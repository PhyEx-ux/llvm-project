#!/usr/bin/env python3
"""mcs251_ld.py -- a small ASxxxx/SDLD-compatible linker for MCS-251.

This is the de-SDCC Step 3 linker: it replaces `sdld` in the production
chain

    llc -filetype=obj  ->  mcs251_ld.py  ->  QEMU

It parses the *full* ASxxxx ASCII .rel record set emitted by sdas251 and
by our own MCS251RELObjectWriter, replicates sdld251's area layout and
relocation arithmetic, and writes Intel HEX.

Usage (aligned with sdld's command-file form):

    python3 mcs251_ld.py -f image.lk [--mcs251-abi] [--dump]
    python3 mcs251_ld.py image.lk ...                    # same thing

Command (.lk) file grammar, as consumed by sdld's parse()/doparse()
(lkmain.c): one directive per line, `;` starts a comment at any token
position, `-e` stops reading.  Recognised switches:

    -i <file>    Intel HEX output; <file> is a separate token (sdld's
                 `case 'i'` does not take an argument -- the first
                 non-option token of the whole command is the output
                 file base, exactly like sdld).  We always write
                 <stem>.hex because the QEMU MCS-251 loader only
                 accepts that suffix.
    -b A = expr  area base address
    -A <sig>     expected MCS-251 ABI signature (requires --mcs251-abi)
    -I <expr>    internal RAM size (default 128)
    -e           end of command file
    -m -M -u -w -x -r ... accepted and ignored where they only affect
                 listings/maps we do not produce.

Semantics replicated from sdld-upstream sources (all references below
are sdcc-upstream/sdas/linksrc/):

  * .rel parsing        lkrel.c/lkhead.c/lksym.c/lkarea.c (newhead,
                        module, newarea, newsym) and lkrloc3.c
                        (relt3/relr3/relp3)
  * area layout         lkarea.c:784 lnkarea2 + lkarea.c:941 lnksect2
                        (the 8051-like path taken because sdld251 has
                        TARGET_IS_8051, see lkmain.c:505) plus
                        lkmain.c:59 Areas51() which pre-creates the
                        default 8051 areas in a fixed order
  * symbol resolution   lksym.c symval: s_addr + defining areax addr;
                        area references use the areax base directly and
                        the T payload already carries the area-relative
                        offset+addend, added big-endian (lkrloc.c
                        adb_2b/adb_3b with hilo=1 from the XH3 prefix)
  * relocation modes    lkrloc3.c relr3; mode bits from aslink.h
  * Intel HEX output    lkout.c ixx/iflush (32-byte record merging,
                        type 04 extended linear address records)
  * strict ABI checks   lkmain.c mcs251_abi_signature_valid /
                        mcs251_check_abi_modules (error strings kept
                        verbatim)

Standard library only; Python 3.6+.
"""

import os
import re
import sys

# ---------------------------------------------------------------------------
# sdld constants (aslink.h)
# ---------------------------------------------------------------------------

ER_WARNING, ER_ERROR, ER_FATAL = 1, 2, 3

# Area flag bits.  NOTE: the C definitions are octal literals; the .rel
# A record carries them as *hexadecimal* text under the XH3 radix.  The
# values below are the numeric equivalents (aslink.h:277-292).
A3_OVR = 0o004          # 4    overlay
A3_ABS = 0o010          # 8    absolute
A3_PAG = 0o020          # 16   paged
A_DATA = 0o0000         # 0
A_CODE = 0o0040         # 32   code space
A_XDATA = 0o0100        # 64   external data space
A_BIT = 0o0200          # 128  bit addressable space
A_NOLOAD = 0o0400       # 256

# Relocation mode bits (aslink.h:309-389).  R3_* are the record bits,
# R_* / IS_* the composite predicates used by relr3 (lkrloc3.c:349-648).
R3_WORD = 0x00
R3_BYTE = 0x01
R3_SYM = 0x02
R3_PCR = 0x04
R3_BYTX = 0x08
R3_USGN = 0x10
R3_PAG0 = 0x20
R3_PAG = 0x40
R3_MSB = 0x80
R_BYT3 = 0x100
R_HIB = 0x200
R_BIT = 0x400
R_ESCAPE_MASK = 0xF0
R_J19_MASK = R3_BYTE | R3_BYTX | R3_MSB
R3_J11 = R3_WORD | R3_BYTX          # 0x08
R3_J19 = R3_WORD | R3_BYTX | R3_MSB # 0x88
R_C24 = R3_WORD | 0x00 | R3_MSB     # 0x80 (R3_BYT1 == 0)
R_MCS251_CONTROL = 0x0800
R_J16 = R_MCS251_CONTROL

def IS_R_J19(m):
    return (m & R_J19_MASK) == R3_J19

def IS_R_J11(m):
    return (m & R_J19_MASK) == R3_J11

def IS_C24(m):
    return (m & R_J19_MASK) == R_C24

def IS_R_J16(m):
    return (m & R_J16) != 0

NTXT = 16              # max T-line values incl. the 3 address bytes
A_BYTES = 3            # XH3: 24-bit T addresses
A_MASK = 0x00FFFFFF
IXXMAXBYTES = 32       # iflush() record size (lkout.c)

ABS_AREA = ".  .ABS."  # lkdata.c _abs_; sdas writes it as ".__.ABS."
                       # but the linker only creates the area under the
                       # internal spelling; symbol matching uses the
                       # raw .rel text, so both spellings stay distinct.

_HEX2 = re.compile(r"^[0-9A-Fa-f]{2}$")  # one ASxxxx value byte token


class LinkError(Exception):
    """Fatal, sdld-style diagnostic (always prefixed ?ASlink-Error-)."""


# ---------------------------------------------------------------------------
# Object model
# ---------------------------------------------------------------------------

class Areax(object):
    """One A record: a per-module slice of an Area (lkdata.c areax)."""

    __slots__ = ("area", "head", "size", "addr")

    def __init__(self, area, head):
        self.area = area
        self.head = head
        self.size = 0
        self.addr = -1  # lkparea: -1 until placed (sdld 8051-like)


class Area(object):
    """A unique named area with its concatenated/overlayed slices."""

    __slots__ = ("name", "flag", "addr", "size", "bset", "areaxs", "unaloc")

    def __init__(self, name):
        self.name = name
        self.flag = 0
        self.addr = 0
        self.size = 0
        self.bset = False
        self.areaxs = []
        self.unaloc = 0

    def loc_index(self):
        """Memory-space selector (lnkarea2, lkarea.c:878-881)."""
        if self.flag & A_CODE:
            return 1
        if self.flag & A_XDATA:
            return 2
        if self.flag & A_BIT:
            return 3
        return 0


class Sym(object):
    def __init__(self, name):
        self.name = name
        self.defined = False
        self.addr = 0
        self.areax = None
        self.module = ""
        self.ref_modules = []  # heads holding an S ... Ref for this sym


class Head(object):
    """One H record: a module's area/symbol index tables."""

    def __init__(self, lfile):
        self.module = ""
        self.lfile = lfile  # None for the synthetic Areas51() header
        self.areas = []     # index -> Areax
        self.syms = []      # index -> Sym
        self.narea = 0
        self.nsym = 0
        self.optsdcc_seen = False
        self.optsdcc_signature = None

    def report_name(self):
        if self.module:
            return self.module
        if self.lfile:
            return self.lfile
        return "<unnamed>"


class Reloc(object):
    __slots__ = ("mode", "tindex", "rindex", "line_no")

    def __init__(self, mode, tindex, rindex, line_no):
        self.mode = mode
        self.tindex = tindex
        self.rindex = rindex
        self.line_no = line_no


class TRecord(object):
    __slots__ = ("values", "flags", "line_no", "areax")

    def __init__(self, values, line_no):
        self.values = values  # includes the 3 address bytes
        self.flags = [1] * len(values)
        self.line_no = line_no
        self.areax = None  # set from the following R line's area index


# ---------------------------------------------------------------------------
# Expression evaluator for -b/-I values (subset of lkeval.c expr/term:
# numbers with 0x/0b/0o/0d/0h prefixes, decimal, parens, the usual
# binary operators, unary -/~, and symbol references).
# ---------------------------------------------------------------------------

_EXPR_TOK = re.compile(r"\s*(0[xX][0-9a-fA-F]+|0[bB][01]+|0[oOqQ][0-7]+|"
                       r"0[dD][0-9]+|0[hH][0-9a-fA-F]+|[0-9]+|"
                       r"[A-Za-z_.][A-Za-z0-9_.]*|<<|>>|[-+*/%&|^~()<>])")

def eval_expr(text, symtab=None):
    tokens = _EXPR_TOK.findall(text)
    rest = _EXPR_TOK.sub("", text).strip()
    if rest:
        raise LinkError("?ASlink-Error-bad expression \"%s\"" % text.strip())
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def take():
        t = peek()
        pos[0] += 1
        return t

    def term():
        t = take()
        if t is None:
            raise LinkError("?ASlink-Error-bad expression \"%s\"" % text.strip())
        if t == "(":
            v = expr(0)
            if take() != ")":
                raise LinkError("?ASlink-Error-Missing delimiter")
            return v
        if t == "-":
            return (~expr(100) + 1) & 0xFFFFFFFF
        if t == "~":
            return ~expr(100) & 0xFFFFFFFF
        if t and t[0].isdigit():
            r, digits = _radix_of(t)
            body = t[2:] if r != 10 else t
            if r == 10 and t[:2].lower() == "0d":
                body = t[2:]
            return int(body, r) & 0xFFFFFFFF
        if symtab is not None and t in symtab:
            s = symtab[t]
            return (s.addr + (s.areax.addr if s.areax else 0)) & 0xFFFFFFFF
        raise LinkError("?ASlink-Error-Undefined symbol %s" % t)

    def _radix_of(tok):
        pre = tok[:2].lower()
        if pre in ("0x", "0h"):
            return 16, tok[2:]
        if pre == "0b":
            return 2, tok[2:]
        if pre in ("0o", "0q"):
            return 8, tok[2:]
        if pre == "0d":
            return 10, tok[2:]
        return 10, tok

    # precedence: | lowest, then ^, then &, then << >>, then + -, then * / %
    def expr(level):
        if level >= 5:
            return term()
        v = expr(level + 1)
        ops = (["|"], ["^"], ["&"], ["<<", ">>"], ["+", "-"], ["*", "/", "%"])[level]
        while True:
            t = peek()
            if t not in ops:
                return v
            take()
            r = expr(level + 1)
            if t == "|":
                v |= r
            elif t == "^":
                v ^= r
            elif t == "&":
                v &= r
            elif t == "<<":
                v <<= r
            elif t == ">>":
                v >>= r
            elif t == "+":
                v += r
            elif t == "-":
                v -= r
            elif t == "*":
                v *= r
            elif t == "/":
                v = v // r if r else 0
            elif t == "%":
                v = v % r if r else 0
            v &= 0xFFFFFFFF

    v = expr(0)
    if peek() is not None:
        raise LinkError("?ASlink-Error-bad expression \"%s\"" % text.strip())
    return v


# ---------------------------------------------------------------------------
# The linker
# ---------------------------------------------------------------------------

class Linker(object):
    def __init__(self, strict_abi=False, dump=False):
        self.strict_abi = strict_abi
        self.dump = dump
        self.lkerr = 0          # counted errors (sdld lkerr)
        self.output_stem = None # first non-option token, minus extension
        self.inputs = []        # remaining non-option tokens (.rel files)
        self.bases = []         # (name, expr_text) from -b
        self.abi_expected = None
        self.iram_size = 128    # -I, sdld default
        # model
        self.areas = []         # ordered area list (areap chain)
        self.area_by_name = {}
        self.heads = []         # in .rel read order
        self.symtab = {}        # name -> Sym (global, like symhash)
        self.image = {}         # pass 2 output: addr -> byte
        self.order = 0
        self.stacksize = 0      # SSEG auto-sizing state (lnksect2 'S')
        self._dchar_val = "a"   # DSEG areax marker letter

    # -- diagnostics -------------------------------------------------------

    def err(self, msg):
        sys.stderr.write(msg + "\n")
        self.lkerr += 1

    # -- command file parsing (lkmain.c parse/doparse) ---------------------

    def parse_command_file(self, path):
        self._parse_lines(self._read_lines(path), path, nested=False)
        if self.output_stem is None and self.inputs:
            # sdld: with only one file token it doubles as both the output
            # base and the single input (lkmain.c:439-446)
            stem, _ext = os.path.splitext(self.inputs[0])
            self.output_stem = stem if stem else self.inputs[0]
        if not self.inputs:
            raise LinkError("?ASlink-Error-no input files")

    @staticmethod
    def _read_lines(path):
        try:
            with open(path, "r") as f:
                return f.read().splitlines()
        except OSError as e:
            raise LinkError("?ASlink-Error-<cannot open> : \"%s\" (%s)"
                            % (path, e.strerror))

    # Argument-taking switches (bassav, iramsav, mcs251_set_abi_expected,
    # ...) consume the rest of the physical line (they keep sdld's `ip`
    # pointing into the current line).
    _ARG_SWITCHES = set("bBgGkKlIAXCSf")

    def _parse_lines(self, lines, path, nested):
        for raw in lines:
            # `;` cuts the line at any token position (parse() returns as
            # soon as it sees one).
            body = raw.split(";", 1)[0]
            tokens = body.split()
            for ti, tok in enumerate(tokens):
                if not tok.startswith("-") or len(tok) == 1:
                    self._file_token(tok)
                    continue
                letters = tok[1:]
                k = 0
                consumed_line = False
                while k < len(letters):
                    c = letters[k]
                    k += 1
                    if c in "bB":
                        rest = self._rest_after(body, tokens, ti)
                        if not rest:
                            raise LinkError(
                                "?ASlink-Error-No area in base expression")
                        self.bases.append(rest)
                        consumed_line = True
                    elif c in "gGkKlL":
                        consumed_line = True  # libraries/globals: ignored
                    elif c == "A":
                        self._set_abi_expected(self._rest_after(body, tokens, ti),
                                               path)
                        consumed_line = True
                    elif c == "I":
                        self.iram_size = self._iram_value(
                            self._rest_after(body, tokens, ti))
                        consumed_line = True
                    elif c in "XxCcS":
                        consumed_line = True  # memory size reports: ignored
                    elif c in "fF":
                        if nested:
                            break  # sdld: "Nested option -f ignored"
                        rest = self._rest_after(body, tokens, ti)
                        if not rest:
                            raise LinkError(
                                "?ASlink-Error-missing command file name")
                        self._parse_lines(self._read_lines(rest), rest, True)
                        consumed_line = True
                    elif c in "eE":
                        return  # -e: end of command input
                    # everything else (-i -m -M -u -w -x -r -n -p -z ...)
                    # only selects formats/listings and is accepted
                    # silently; like sdld, `-i out` makes the following
                    # token the output base via _file_token().
                if consumed_line:
                    break
        return

    @staticmethod
    def _rest_after(body, tokens, ti):
        """Text following option token `ti` on this physical line."""
        pos = 0
        for idx, t in enumerate(tokens):
            pos = body.find(t, pos)
            if idx == ti:
                return body[pos + len(t):].strip()
            pos += len(t)
        return ""

    def _iram_value(self, rest):
        if not rest:
            return 128
        v = eval_expr(rest)
        if v <= 0 or v > 256:
            return 128
        return v

    def _file_token(self, tok):
        if self.output_stem is None:
            # First non-option token is the output base (parse() F_OUT);
            # lkfopen() strips the extension and appends ".ihx" (we append
            # ".hex" because that is what the QEMU loader accepts).
            stem, _ext = os.path.splitext(tok)
            self.output_stem = stem if stem else tok
        else:
            self.inputs.append(tok)

    def _set_abi_expected(self, signature, where):
        if not self.strict_abi:
            # sdld parse(): -A without --mcs251-abi falls into "unknown
            # option" and is ignored with a warning.
            sys.stderr.write("?ASlink-Warning-Unkown option -A ignored\n")
            return
        if self.abi_expected is not None:
            self.err("?ASlink-Error-MCS251 ABI expected signature specified "
                     "more than once.")
            return
        signature = signature.strip()
        if not mcs251_abi_signature_valid(signature):
            self.err("?ASlink-Error-MCS251 ABI expected signature is invalid.")
            return
        self.abi_expected = signature

    # -- pass 1: read .rel files (lkrel.c load_rel + link_main) ------------

    def read_all_rels(self):
        # Areas51() runs before every pass (lkmain.c:465) and pre-creates
        # the default areas in this exact order.
        self.areas51()
        for path in self.inputs:
            self.read_rel(path)

    def areas51(self):
        """lkmain.c:59 Areas51 -- default 8051 areas, in order, with the
        fixed register-bank / bit-byte base addresses."""
        synthetic = Head(None)
        self.heads.append(synthetic)
        # Feed the exact A records Areas51() feeds itself.
        for name, size, flags in [
                ("_CODE", 0, 0),
                ("REG_BANK_0", 0, 4), ("REG_BANK_1", 0, 4),
                ("REG_BANK_2", 0, 4), ("REG_BANK_3", 0, 4),
                ("BSEG", 0, 0x80),
                ("BSEG_BYTES", 0, 0),
                ("BIT_BANK", 0, 4),
                ("DSEG", 0, 0),
                ("OSEG", 0, 4),
                ("ISEG", 0, 0),
                ("SSEG", 0, 4)]:
            self.new_area(synthetic, name, size, flags, 0)
        for name, addr in [("REG_BANK_0", 0x00), ("REG_BANK_1", 0x08),
                           ("REG_BANK_2", 0x10), ("REG_BANK_3", 0x18),
                           ("BSEG_BYTES", 0x20)]:
            self.area_by_name[name].addr = addr
            self.area_by_name[name].bset = True
        # l_IRAM symbol (Areas51 tail)
        s = self.lkpsym("l_IRAM", create=True)
        s.defined = True
        s.addr = self.iram_size if 0 < self.iram_size <= 0x100 else 0x100
        s.areax = None

    def lkparea(self, name, head):
        """lkarea.c:213 lkparea -- find or create the unique area, append
        an areax owned by this head."""
        area = self.area_by_name.get(name)
        if area is None:
            area = Area(name)
            self.area_by_name[name] = area
            self.areas.append(area)
        ax = Areax(area, head)
        area.areaxs.append(ax)
        return area, ax

    def lkpsym(self, name, create):
        s = self.symtab.get(name)
        if s is not None or not create:
            return s
        s = Sym(name)
        self.symtab[name] = s
        return s

    def read_rel(self, path):
        # Fail-closed reads: unlike sdld (which silently skips anything it
        # does not know and accepts a missing final newline / dangling T),
        # every structural defect is a loud, non-zero-exit error.  A legal
        # file is always a whole number of newline-terminated records.
        try:
            with open(path, "r") as f:
                data = f.read()
        except OSError as e:
            raise LinkError("?ASlink-Error-<cannot open> : \"%s\" (%s)"
                            % (path, e.strerror))
        if data and not data.endswith("\n"):
            raise LinkError("?ASlink-Error-truncated record at EOF: last "
                            "line of \"%s\" is not newline-terminated" % path)
        lines = data.splitlines()
        if not lines or not lines[0].startswith(("XH3", "XL3", "XH2", "XL2",
                                                 "XH4", "XL4", "DH", "QH")):
            raise LinkError("?ASlink-Error-not an ASxxxx .rel file: \"%s\""
                            % path)
        if not lines[0].startswith("XH3"):
            raise LinkError("?ASlink-Error-unsupported .rel header \"%s\" "
                            "(only XH3 / 24-bit big-endian is supported): \"%s\""
                            % (lines[0].strip(), path))
        head = None
        ax = None
        dangling_t = 0  # line number of a T record still awaiting its R
        dump = RelDump(path) if self.dump else None
        for no, line in enumerate(lines[1:], start=2):
            toks = line.split()
            if not toks:
                continue
            kind = toks[0]
            if dangling_t and kind != "R":
                # sdas always closes a T with its R line (even an empty
                # "R 00 00 aa aa"); anything else means the file was cut.
                raise LinkError("?ASlink-Error-truncated record 'T' "
                                "(%s:%d): no following 'R' record"
                                % (path, dangling_t))
            if kind == "H":
                head = Head(path)
                self.heads.append(head)
                self.parse_header(head, toks, path, no)
                ax = self.abs_areax(head)
            elif kind == "M":
                if head is None:
                    raise LinkError("?ASlink-Error-No header defined (%s:%d)"
                                    % (path, no))
                head.module = toks[1] if len(toks) > 1 else ""
            elif kind == "O":
                if head is not None and self.strict_abi:
                    sig = line[1:].strip()
                    if head.optsdcc_seen:
                        self.err('?ASlink-Error-MCS251 ABI signature specified '
                                 'more than once in module "%s".'
                                 % head.report_name())
                    elif not mcs251_abi_signature_valid(sig):
                        head.optsdcc_seen = True
                        self.err('?ASlink-Error-MCS251 ABI signature rejected '
                                 'in module "%s".' % head.report_name())
                    else:
                        head.optsdcc_seen = True
                        head.optsdcc_signature = sig
            elif kind == "S":
                self.new_symbol(head, ax, toks, path, no)
            elif kind == "A":
                try:
                    ax = self.new_area(head, toks[1],
                                       int(toks[3], 16),
                                       int(toks[5], 16),
                                       int(toks[7], 16))
                except (ValueError, IndexError):
                    raise LinkError("?ASlink-Error-A input error (%s:%d): %s"
                                    % (path, no, " ".join(toks)))
                if dump:
                    dump.areas.append((toks[1], int(toks[3], 16),
                                       int(toks[5], 16)))
            elif kind in ("T", "R", "P"):
                # Byte-level truncation guard: ASxxxx encodes every T/R
                # value byte as exactly two hex digits; a one-digit token
                # means the line was cut mid-field (sdld would quietly
                # parse the narrower value).
                for t in toks[1:]:
                    if len(t) != 2 or not _HEX2.match(t):
                        raise LinkError("?ASlink-Error-truncated record '%s' "
                                        "(%s:%d): \"%s\" is not a two-hex-"
                                        "digit byte" % (kind, path, no, t))
                if dump:
                    dump.record(head, kind, toks, no)
                # Full processing happens in pass 2; pass 1 only builds
                # the model, but T/R syntax is validated eagerly so bad
                # inputs fail before layout.
                if kind == "T":
                    dangling_t = no
                else:
                    dangling_t = 0
                    if kind == "R":
                        self._scan_r_line(head, toks, path, no)
            else:
                raise LinkError("?ASlink-Error-unsupported record '%s' "
                                "(%s:%d)" % (kind, path, no))
        if dangling_t:
            raise LinkError("?ASlink-Error-truncated record 'T' "
                            "(%s:%d): no following 'R' record"
                            % (path, dangling_t))
        if dump:
            dump.report()

    def abs_areax(self, head):
        """newhead() (lkhead.c:96) creates the absolute pseudo area first;
        its areax becomes the symbol anchor for S records seen before any
        A record.  It is deliberately NOT entered into head.areas (sdld's
        newhead leaves hp->a_list untouched for _abs_), so R area index 0
        is the first real A record of the module."""
        area, ax = self.lkparea(ABS_AREA, head)
        area.flag = A3_ABS
        ax.addr = 0
        return ax

    def parse_header(self, head, toks, path, no):
        i = 1
        seen = set()
        while i < len(toks):
            try:
                val = int(toks[i], 16)
            except ValueError:
                i += 1
                continue
            if i + 1 < len(toks):
                if toks[i + 1] == "areas":
                    head.narea = val
                    seen.add("areas")
                elif toks[i + 1] == "global":
                    head.nsym = val
                    seen.add("global")
            i += 2
        # "H n areas n global symbols" is the fixed sdas251/llc shape; a
        # missing half means the record was truncated.
        if seen != {"areas", "global"}:
            raise LinkError("?ASlink-Error-truncated record 'H' (%s:%d): "
                            "missing area/global symbol counts"
                            % (path, no))

    def _head_add_area(self, head, ax):
        if len(head.areas) >= max(head.narea, 1) and head.narea:
            self.err("?ASlink-Error-Header area list overflow")
            return
        head.areas.append(ax)

    def _head_add_sym(self, head, s):
        if head.nsym and len(head.syms) >= head.nsym:
            self.err("?ASlink-Error-Header symbol list overflow")
            return
        head.syms.append(s)

    def new_area(self, head, name, size, flags, addr):
        """lkarea.c:114 newarea (sdld 8051-like path: flags then addr)."""
        # These two areas are a load/run pair owned by the LLVM globals ABI.
        # A mismatched flag would either load bytes into internal RAM or reserve
        # the ROM image as NOLOAD, both silent firmware corruption. Reject it
        # before layout instead of inheriting sdld's permissive merge behavior.
        expected = {"DSEG": A_DATA, "XINIT": A_CODE}.get(name)
        if expected is not None and flags != expected:
            raise LinkError("?ASlink-Error-MCS251 %s area flags 0x%X do not "
                            "match required 0x%X" % (name, flags, expected))
        area, ax = self.lkparea(name, head)
        ax.size = size
        if len(area.areaxs) == 1:
            area.flag = flags
        elif flags and area.flag != flags:
            # sdld 8051-like linkers do not report conflicting area flags
            # (lkarea.c:150 guards it with !is_sdld() || Z80-like); keep
            # the first flags and stay silent, exactly like sdld251.
            pass
        ax.addr = addr  # sdas emits addr 0; the linker recomputes it
        self._head_add_area(head, ax)
        return ax

    def new_symbol(self, head, ax, toks, path, no):
        """lksym.c:143 newsym."""
        if head is None:
            raise LinkError("?ASlink-Error-No header defined (%s:%d)"
                            % (path, no))
        # lksym.c newsym reads the kind letter then skips exactly two
        # characters ("ef" of Ref/Def) before evaluating the value.  The
        # value itself must be present (sdas emits six digits); an empty
        # field means the record was truncated.
        m = re.match(r"^S\s+(\S+)\s+([RD])..([0-9A-Fa-f]+)$", " ".join(toks))
        if not m:
            raise LinkError("?ASlink-Error-Invalid or truncated S record "
                            "(%s:%d): %s" % (path, no, " ".join(toks)))
        name, kind, val = m.group(1), m.group(2), int(m.group(3), 16)
        s = self.lkpsym(name, create=True)
        if kind == "R":
            if val:
                self.err("?ASlink-Error-Non zero S_REF")
            if head not in s.ref_modules:
                s.ref_modules.append(head)
        else:
            if s.defined and not (s.addr == val and s.areax is not None and
                                  s.areax.area.flag & A3_ABS):
                self.err("?ASlink-Error-Multiple definition of %s" % name)
            s.defined = True
            s.addr = val
            s.areax = ax
            s.module = head.report_name()
        self._head_add_sym(head, s)

    def _scan_r_line(self, head, toks, path, no):
        """Validate an R line (relr3 prologue, lkrloc3.c:292-306)."""
        if len(toks) < 5:
            raise LinkError("?ASlink-Error-R input error (%s:%d)" % (path, no))
        try:
            if int(toks[1], 16) != (R3_WORD | 0) or int(toks[2], 16) != 0:
                self.err("?ASlink-Error-R input error (%s:%d)" % (path, no))
                return
            aindex = int(toks[3], 16) * 256 + int(toks[4], 16)
            if aindex >= max(head.narea, len(head.areas)):
                self.err("?ASlink-Error-R area error (%s:%d)" % (path, no))
                return
            j = 5
            while j < len(toks):
                mode = int(toks[j], 16)
                j += 1
                if (mode & R_ESCAPE_MASK) == R_ESCAPE_MASK:
                    mode = ((mode & ~R_ESCAPE_MASK) << 8) | int(toks[j], 16)
                    j += 1
                tindex = int(toks[j], 16)
                j += 1
                rindex = int(toks[j], 16) * 256 + int(toks[j + 1], 16)
                j += 2
                if mode & R3_SYM:
                    if rindex >= max(head.nsym, len(head.syms)):
                        self.err("?ASlink-Error-R symbol error (%s:%d)"
                                 % (path, no))
                        return
                elif not ((IS_R_J11(mode) or IS_R_J19(mode)) and
                          rindex == 0xFFFF):
                    if rindex >= max(head.narea, len(head.areas)):
                        self.err("?ASlink-Error-R area error (%s:%d)"
                                 % (path, no))
                        return
        except (ValueError, IndexError):
            raise LinkError("?ASlink-Error-R input error (%s:%d)" % (path, no))

    # -- strict ABI ---------------------------------------------------------

    def check_abi_modules(self):
        """lkmain.c:211 mcs251_check_abi_modules."""
        if not self.strict_abi:
            return
        if self.abi_expected is None:
            self.err("?ASlink-Error-MCS251 ABI expected signature was not "
                     "provided by the link driver.")
            return
        for head in self.heads:
            if head.lfile is None:
                continue  # Areas51() synthetic header
            if not head.optsdcc_seen:
                self.err('?ASlink-Error-MCS251 ABI signature missing in module'
                         ' "%s"; every input must contain .optsdcc.'
                         % head.report_name())
            elif head.optsdcc_signature is not None and \
                    self.abi_expected != head.optsdcc_signature:
                self.err('?ASlink-Error-MCS251 ABI mismatch: module "%s" does'
                         ' not match the link driver expectation.'
                         % head.report_name())

    # -- layout (lkarea.c lnkarea2/lnksect2) --------------------------------

    def setarea(self):
        """lkarea.c:741 setarea -- apply -b bases."""
        for text in self.bases:
            m = re.match(r"^\s*([A-Za-z0-9_.]+)\s*=\s*(.+)$", text)
            if not m:
                self.err("ASlink-Error-No '=' in base expression")
                continue
            name, expr = m.group(1), m.group(2)
            area = self.area_by_name.get(name)
            if area is None:
                self.err("ASlink-Error-No definition of area %s" % name)
            else:
                area.addr = eval_expr(expr, self.symtab) & A_MASK
                area.bset = True

    class _Bitmap(object):
        """32-bit-element MSB-first bitmap mimicking the C unsigned long
        arrays used by find_empty_space/allocate_space (lkarea.c:522-602).
        All arithmetic is masked to 32 bits to reproduce C wrap-around."""

        def __init__(self, nbits):
            self.elems = [0] * ((nbits + 31) // 32)

        def find_empty_space(self, start, size, name):
            mapn = len(self.elems)
            while True:
                a = start
                i = start >> 5
                j = (start + size) >> 5
                mask = (-(1 << (start & 0x1F))) & 0xFFFFFFFF
                if j > mapn:
                    sys.stderr.write(
                        "?ASlink-Error-internal memory limit is exceeded for "
                        "%s; memory size = 0x%06X, address = 0x%06X\n"
                        % (name, mapn << 5, start + size - 1))
                    break
                while i < j:
                    if self.elems[i] & mask:
                        k = 32
                        b = 0x80000000
                        while b != 0:
                            if self.elems[i] & b:
                                break
                            b >>= 1
                            k -= 1
                        start = a + k
                        break
                    i += 1
                    mask = 0xFFFFFFFF
                    a += 32
                if start > a:
                    continue
                mask &= ((1 << ((start + size) & 0x1F)) - 1) & 0xFFFFFFFF
                if i < mapn and self.elems[i] & mask:
                    k = 32
                    b = 0x80000000
                    while b != 0:
                        if self.elems[i] & b:
                            break
                        b >>= 1
                        k -= 1
                    start = (a & ~0x1F) + k
                if start <= a:
                    break
            return start

        def allocate_space(self, start, size, name):
            mapn = len(self.elems)
            a = start
            i = start >> 5
            j = (start + size) >> 5
            mask = (-(1 << (start & 0x1F))) & 0xFFFFFFFF
            if j > mapn:
                sys.stderr.write(
                    "?ASlink-Error-internal memory limit is exceeded for %s; "
                    "memory size = 0x%06X, address = 0x%06X\n"
                    % (name, mapn << 5, start + size - 1))
            else:
                while i < j:
                    if self.elems[i] & mask:
                        sys.stderr.write("?ASlink-Error-memory overlap near "
                                         "0x%X for %s\n" % (a, name))
                    self.elems[i] |= mask
                    i += 1
                    mask = 0xFFFFFFFF
                    a += 32
                mask &= ((1 << ((start + size) & 0x1F)) - 1) & 0xFFFFFFFF
                if i < mapn and self.elems[i] & mask:
                    sys.stderr.write("?ASlink-Error-memory overlap near 0x%X "
                                     "for %s\n" % (a, name))
                self.elems[i] |= mask
            return start

    def lnkarea2(self):
        """lkarea.c:784 lnkarea2 -- the 8051-like allocator."""
        idatamap = [" "] * 256
        codemap = self._Bitmap(0x1000000)   # codemap8051: 4 MiB bits
        xdatamap = self._Bitmap(0x1000000)  # xdatamap
        rloc = [0, 0, 0, 0]
        # Absolute slices lose their origin during relocation layout. Preserve
        # their physical bounds for the opt-in G12K128 stack contract.
        absolute_data_ends = [ax.addr + ax.size for ap in self.areas
                              if ap.flag & A3_ABS and ap.loc_index() == 0
                              for ax in ap.areaxs if ax.size]

        # 1. sort all absolute areas to the front, reversed (lkarea.c:801-818)
        i = 0
        while i < len(self.areas) - 1:
            if self.areas[i + 1].flag & A3_ABS:
                absap = self.areas.pop(i + 1)
                self.areas.insert(0, absap)
            else:
                i += 1

        # 2. GSINIT*/GSFINAL accumulation into GSINIT0 + BSEG bookkeeping
        #    + DSEG/ISEG base capture (lkarea.c:820-872)
        gs_size = 0
        gs0 = bseg = bseg_bytes = None
        dseg = None
        dram_start = iram_start = 0
        last_abs = 0
        for idx, ap in enumerate(self.areas):
            if ap.flag & A3_ABS:
                last_abs = idx
            if ap.name.startswith("GS"):
                if ap.size == 0:
                    for ax in ap.areaxs:
                        ap.size += ax.size
                gs_size += ap.size
                if ap.name == "GSINIT0":
                    gs0 = ap
            elif ap.name == "BSEG":
                bseg = ap
                bseg_bytes = self.areas[idx + 1] if idx + 1 < len(self.areas) else None
                # BSEG areax bit sizes accumulate into the area
                for ax in ap.areaxs:
                    ap.size += ax.size
            elif ap.name == "DSEG":
                dseg = ap
                dram_start = ap.addr
            elif ap.name == "ISEG":
                iram_start = ap.addr
        if gs0 is not None:
            gs0.size = gs_size
        if bseg is not None and bseg_bytes is not None and bseg_bytes.name == "BSEG_BYTES":
            bseg_bytes.areaxs[0].size = (bseg.addr + bseg.size + 7) // 8
            # move BSEG_BYTES directly after the last absolute area
            self.areas.remove(bseg_bytes)
            self.areas.insert(last_abs + 1, bseg_bytes)

        # 3. main allocation loop (lkarea.c:874-928)
        for ap in self.areas:
            loc = ap.loc_index()
            if ap.flag & A3_ABS:
                self.lnksect2(ap, loc, idatamap, codemap, xdatamap,
                              dram_start, iram_start)
            else:
                if not ap.bset:
                    ap.addr = rloc[loc]
                    ap.bset = True
                rloc[loc] = self.lnksect2(ap, loc, idatamap, codemap,
                                          xdatamap, dram_start, iram_start)
            if ap.name == "BSEG_BYTES" and ap.areaxs[0].addr >= 0x20 and bseg is not None:
                bseg.addr += (ap.areaxs[0].addr - 0x20) * 8
            if ap.name != ABS_AREA:
                s = self.lkpsym("s_" + ap.name, create=True)
                s.defined = True
                s.addr = ap.addr
                s.areax = None
                s = self.lkpsym("l_" + ap.name, create=True)
                s.defined = True
                s.addr = ap.size
                s.areax = None
        # 4. recompute the DSEG usage summary (lkarea.c:930-938)
        if dseg is not None:
            dseg.addr = 0
            dseg.size = sum(1 for j in range(0x80) if idatamap[j] != " ")
            s = self.symtab.get("s_DSEG")
            if s is not None:
                s.addr = 0
            s = self.symtab.get("l_DSEG")
            if s is not None and dseg is not None:
                s.addr = dseg.size
        self._define_mcs251_stack_base(absolute_data_ends)

    def _define_mcs251_stack_base(self, absolute_data_ends):
        """Self-start opt-in: place an upward stack in G12K128's 4K EDATA.

        s_DSEG/l_DSEG are SDLD usage summaries, not a physical high-water
        mark. Use every byte-addressed internal slice (including OSEG/ISEG
        and register/bit-bank reservations), not an area size sum.
        """
        symbol = self.symtab.get("__mcs251_stack_base")
        if symbol is None or not symbol.ref_modules:
            return  # Existing SDCC/QEMU-only chains retain their old layout.
        if symbol.defined:
            raise LinkError("?ASlink-Error-__mcs251_stack_base is reserved for "
                            "the linker")
        ends = list(absolute_data_ends)
        ends.extend(ax.addr + ax.size for ap in self.areas
                    if ap.loc_index() == 0 and not ap.flag & A3_ABS
                    for ax in ap.areaxs if ax.size)
        data_end = max([0x100] + ends)
        first_byte = ((data_end + 15) & ~15) + 16
        if first_byte > 0x0C00:
            raise LinkError("?ASlink-Error-MCS251 stack capacity: data end "
                            "0x%04X leaves fewer than 1024 bytes in 4K EDATA"
                            % data_end)
        symbol.defined = True
        symbol.addr = first_byte - 1
        symbol.areax = None
        sys.stderr.write("MCS251 stack: data end=0x%04X SPX=0x%04X "
                         "capacity=%d bytes (EDATA end=0x0FFF)\n"
                         % (data_end, symbol.addr, 0x1000 - first_byte))

    def lnksect2(self, ap, loc, idatamap, codemap, xdatamap,
                 dram_start, iram_start):
        """lkarea.c:941 lnksect2 -- place the areaxs of one area.

        Returns the running address for rloc[] (the C return value)."""
        ap.unaloc = 0
        if ap.name in ("ISEG", "SSEG"):
            ramstart = iram_start
            ramlimit = 0x100 if not (self.iram_size <= 0 or
                                     ramstart + self.iram_size > 0x100) \
                else ramstart + self.iram_size
        else:
            ramstart = dram_start
            ramlimit = 0x80 if (self.iram_size <= 0 or
                                ramstart + self.iram_size > 0x80) \
                else ramstart + self.iram_size

        size = 0
        addr = ap.addr
        fchar = " "
        if loc == 0:
            fchar = {"DSEG": "D", "ISEG": "I", "SSEG": "S", "OSEG": "Q",
                     "REG_BANK_0": "0", "REG_BANK_1": "1", "REG_BANK_2": "2",
                     "REG_BANK_3": "3", "BSEG_BYTES": "B",
                     "BIT_BANK": "T"}.get(ap.name, " ")
        elif loc == 1 and ap.name == "GSINIT":
            fchar = "G"
        elif loc == 2 and ap.name == "XSTK":
            fchar = "K"

        if ap.flag & A3_OVR:
            for ax in ap.areaxs:
                if ax.size == 0:
                    continue
                if fchar in "0123":
                    addr = (int(fchar) - 0) * 8
                    ax.addr = addr
                    size = ax.size
                    for j in range(addr, min(addr + size, ramlimit)):
                        idatamap[j] = fchar
                elif fchar in ("S", "Q"):
                    used = sum(1 for j in range(ramstart, ramlimit)
                               if idatamap[j] == fchar)
                    if fchar == "S" and self.stacksize == 0:
                        k = 0
                        ax.size = 0
                        for j in range(ramstart, ramlimit):
                            if idatamap[j] == " ":
                                k += 1
                                if k > ax.size:
                                    ax.size = k
                            else:
                                k = 0
                        self.stacksize = ax.size
                    if ax.size > used:
                        size = ax.size
                        for j in range(ramstart, ramlimit):
                            if idatamap[j] == fchar:
                                idatamap[j] = " "
                        j = ramstart
                        k = 0
                        while j < ramlimit:
                            k = k + 1 if idatamap[j] == " " else 0
                            if k == ax.size:
                                break
                            j += 1
                        if k == ax.size:
                            addr = j - k + 1
                            for jj in range(addr, addr + size):
                                idatamap[jj] = fchar
                        else:
                            ap.unaloc = ax.size
                            self.err("?ASlink-Error-Could not get %d "
                                     "consecutive byte%s in internal RAM for "
                                     "area %s." % (ax.size,
                                                   "s" if ax.size > 1 else "",
                                                   ap.name))
                        if fchar == "S":
                            break
                elif fchar == "T":
                    used = sum(1 for j in range(0x20, 0x30)
                               if idatamap[j] == fchar)
                    if ax.size > used:
                        size = ax.size
                        for j in range(0x20, 0x30):
                            if idatamap[j] == fchar:
                                idatamap[j] = " "
                        j = 0x20
                        k = 0
                        while j < 0x30:
                            k = k + 1 if idatamap[j] == " " else 0
                            if k == ax.size:
                                break
                            j += 1
                        if k == size:
                            addr = j - k + 1
                            for jj in range(addr, addr + size):
                                idatamap[jj] = fchar
                        else:
                            ap.unaloc = ax.size
                            self.err("?ASlink-Error-Could not get %d "
                                     "consecutive byte%s in internal RAM for "
                                     "area %s." % (ax.size,
                                                   "s" if ax.size > 1 else "",
                                                   ap.name))
                else:
                    ax.addr = addr
                    if ax.size > size:
                        size = ax.size
            for ax in ap.areaxs:
                ax.addr = addr
        elif ap.flag & A3_ABS:
            for ax in ap.areaxs:
                if loc == 0:
                    for j in range(ax.addr, min(ax.addr + ax.size, 256)):
                        if idatamap[j] == " ":
                            idatamap[j] = "A"
                        else:
                            sys.stderr.write(
                                "?ASlink-Error-memory overlap at 0x%X for %s\n"
                                % (j, ap.name))
                elif loc == 1:
                    codemap.allocate_space(ax.addr, ax.size, ap.name)
                elif loc == 2:
                    xdatamap.allocate_space(ax.addr, ax.size, ap.name)
                ax.addr = 0  # relative addresses become absolute
                size += ax.size
        else:
            # concatenated sections
            if loc == 1 and ap.size:
                addr = codemap.find_empty_space(addr, ap.size, ap.name)
            if loc == 2 and ap.size:
                addr = xdatamap.find_empty_space(addr, ap.size, ap.name)
            for ax in ap.areaxs:
                if ax.size:
                    if fchar in ("D", "I"):
                        j = ramstart
                        k = 0
                        while j < ramlimit:
                            k = k + 1 if idatamap[j] == " " else 0
                            if k == ax.size:
                                break
                            j += 1
                        if k == ax.size:
                            ax.addr = j - k + 1
                            size += ax.size
                            for jj in range(ax.addr,
                                            min(ax.addr + ax.size, ramlimit)):
                                idatamap[jj] = self._dchar()
                            self._advance_dchar()
                        else:
                            ax.addr = addr
                            addr += ax.size
                            size += ax.size
                            ap.unaloc += ax.size
                            self.err("?ASlink-Error-Could not get %d "
                                     "consecutive byte%s in internal RAM for "
                                     "area %s." % (ax.size,
                                                   "s" if ax.size > 1 else "",
                                                   ap.name))
                    elif fchar == "B":
                        j = 0x20
                        k = 0
                        while j < 0x30:
                            k = k + 1 if idatamap[j] == " " else 0
                            if k == ax.size:
                                break
                            j += 1
                        if k == ax.size:
                            ax.addr = j - k + 1
                            for jj in range(ax.addr, min(ax.addr + ax.size, 0x30)):
                                idatamap[jj] = fchar
                        else:
                            ap.unaloc = ax.size
                            self.err("?ASlink-Error-Could not get %d "
                                     "consecutive byte%s in internal RAM for "
                                     "area %s." % (ax.size,
                                                   "s" if ax.size > 1 else "",
                                                   ap.name))
                        size += ax.size
                    else:
                        # BIT, CODE and XRAM areaxs
                        if fchar == "K" and ax.size == 1:
                            ax.size = 256 - (addr & 0xFF)
                        if loc == 1:
                            addr = codemap.find_empty_space(addr, ax.size,
                                                            ap.name)
                            codemap.allocate_space(addr, ax.size, ap.name)
                        if loc == 2:
                            addr = xdatamap.find_empty_space(addr, ax.size,
                                                             ap.name)
                            xdatamap.allocate_space(addr, ax.size, ap.name)
                        ax.addr = addr
                        addr += ax.size
                        size += ax.size
                else:
                    ax.addr = addr
        ap.size = size
        ap.addr = ap.areaxs[0].addr
        for ax in ap.areaxs:
            if ax.size:
                ap.addr = ax.addr
                break
        if (ap.flag & A3_PAG) and size > 256:
            sys.stderr.write("\n?ASlink-Warning-Paged Area %s Length Error\n"
                             % ap.name)
            self.lkerr += 1
        if (ap.flag & A3_PAG) and ap.size and \
                (ap.addr & 0xFFFFFF00) != ((addr - 1) & 0xFFFFFF00):
            sys.stderr.write("\n?ASlink-Error-Paged Area %s Boundary Error\n"
                             % ap.name)
            self.lkerr += 1
        return addr

    # dchar: the letter used to mark each DSEG areax ('a'..'z', then 'D')

    def _dchar(self):
        c = self._dchar_val
        return c if ("a" <= c <= "z") else "D"

    def _advance_dchar(self):
        c = chr(ord(self._dchar_val) + 1)
        if c < "a" or c > "z":
            c = "D"
        self._dchar_val = c

    # -- undefined globals (lksym.c symdef/symmod) ---------------------------

    def symdef(self):
        """Report undefined globals; mirrors lksym.c:317 symdef + symmod."""
        for s in self.symtab.values():
            if not s.defined:
                for head in s.ref_modules:
                    sys.stderr.write(
                        "\n?ASlink-Warning-Undefined Global %s referenced by "
                        "module %s\n" % (s.name, head.report_name()))
                    self.lkerr += 1

    def symval(self, s):
        v = s.addr
        if s.areax is not None:
            v += s.areax.addr
        return v & 0xFFFFFFFF

    # -- pass 2: relocation + output ----------------------------------------

    def relocate_and_output(self, out_path):
        self.image = {}
        for path in self.inputs:
            self._relocate_file(path)
        self._validate_mcs251_xinit()
        self._write_ihx(out_path)

    def _validate_mcs251_xinit(self):
        """Validate the sparse LLVM load-image protocol when its runtime is
        linked. A generic SDCC image may use XINIT differently, so the check is
        gated on the self-start runtime's defining symbol."""
        runtime = self.symtab.get("__mcs251_globals_init")
        if runtime is None or not runtime.defined:
            return
        xinit = self.area_by_name.get("XINIT")
        dseg = self.area_by_name.get("DSEG")
        if xinit is None or dseg is None:
            raise LinkError("?ASlink-Error-MCS251 globals runtime requires "
                            "DSEG and XINIT areas")
        if xinit.size > 0xFFFF:
            raise LinkError("?ASlink-Error-MCS251 XINIT size 0x%X exceeds "
                            "the 16-bit startup counter" % xinit.size)
        pos = xinit.addr
        end = pos + xinit.size
        dseg_ranges = [(ax.addr, ax.addr + ax.size) for ax in dseg.areaxs
                       if ax.size]
        while pos < end:
            if end - pos < 6:
                raise LinkError("?ASlink-Error-truncated MCS251 XINIT record "
                                "at 0x%06X" % pos)
            try:
                target = (self.image[pos] << 8) | self.image[pos + 1]
                obj_size = (self.image[pos + 2] << 8) | self.image[pos + 3]
                payload = (self.image[pos + 4] << 8) | self.image[pos + 5]
            except KeyError:
                raise LinkError("?ASlink-Error-hole in MCS251 XINIT record "
                                "at 0x%06X" % pos)
            if obj_size == 0 or payload not in (0, obj_size):
                raise LinkError("?ASlink-Error-invalid MCS251 XINIT sizes "
                                "object=%d payload=%d at 0x%06X"
                                % (obj_size, payload, pos))
            if pos + 6 + payload > end:
                raise LinkError("?ASlink-Error-truncated MCS251 XINIT payload "
                                "at 0x%06X" % pos)
            if target + obj_size > 0x10000 or not any(
                    lo <= target and target + obj_size <= hi
                    for lo, hi in dseg_ranges):
                raise LinkError("?ASlink-Error-MCS251 XINIT target "
                                "0x%04X..0x%04X is outside its DSEG slice"
                                % (target, target + obj_size))
            for addr in range(pos + 6, pos + 6 + payload):
                if addr not in self.image:
                    raise LinkError("?ASlink-Error-hole in MCS251 XINIT "
                                    "payload at 0x%06X" % addr)
            pos += 6 + payload

    def _relocate_file(self, path):
        lines = self._read_lines(path)
        # heads appear in read order for this file (library members could
        # contribute several; our chain always has exactly one).
        file_heads = [h for h in self.heads if h.lfile == path]
        hi = 0
        trec = None
        for no, line in enumerate(lines[1:], start=2):
            toks = line.split()
            if not toks:
                continue
            kind = toks[0]
            if kind == "H":
                if hi >= len(file_heads):
                    raise LinkError("?ASlink-Error-No header defined (%s:%d)"
                                    % (path, no))
                head = file_heads[hi]
                hi += 1
                trec = None
                continue
            head = file_heads[hi - 1] if hi else None
            if kind in ("M", "O", "S", "A"):
                continue
            if kind == "T":
                for t in toks[1:]:
                    if len(t) != 2 or not _HEX2.match(t):
                        raise LinkError("?ASlink-Error-truncated record 'T' "
                                        "(%s:%d): \"%s\" is not a two-hex-"
                                        "digit byte" % (path, no, t))
                vals = [int(t, 16) for t in toks[1:]]
                if len(vals) < A_BYTES or len(vals) > NTXT:
                    raise LinkError("?ASlink-Error-T input error (%s:%d)"
                                    % (path, no))
                trec = TRecord(vals, no)
                continue
            if kind == "R":
                if trec is None:
                    raise LinkError("?ASlink-Error-R record without T "
                                    "(%s:%d)" % (path, no))
                self._apply_relocs(head, trec, toks, path, no)
                self._emit_trecord(trec)
                trec = None
                continue
            if kind == "P":
                # Paged-area definitions are unused by our chain; sdas251
                # -plosgff does not emit them either. Accept and ignore.
                continue

    def _apply_relocs(self, head, trec, toks, path, no):
        """lkrloc3.c:274 relr3 (XH3: hilo=1, a_bytes=3, pcb=1)."""
        rtval = trec.values
        rtflg = trec.flags
        aindex = int(toks[3], 16) * 256 + int(toks[4], 16)
        areax = head.areas[aindex]
        trec.areax = areax
        # relocate address: pc = areax base + T-line offset
        rtbase = self._be(rtval, 0, A_BYTES)
        pc = (areax.addr + rtbase) & A_MASK
        rtofst = A_BYTES
        j = 5
        while j < len(toks):
            mode = int(toks[j], 16)
            j += 1
            if (mode & R_ESCAPE_MASK) == R_ESCAPE_MASK:
                mode = ((mode & ~R_ESCAPE_MASK) << 8) | int(toks[j], 16)
                j += 1
            rtp = int(toks[j], 16)
            j += 1
            rindex = int(toks[j], 16) * 256 + int(toks[j + 1], 16)
            j += 2

            error = 0
            if mode & R3_SYM:
                reli = self.symval(head.syms[rindex])
            elif (IS_R_J11(mode) or IS_R_J19(mode)) and rindex == 0xFFFF:
                reli = 0
            else:
                reli = head.areas[rindex].addr

            if mode & R3_PCR:
                if mode & R3_BYTE:
                    reli -= (pc + (rtp - rtofst) + 1)
                else:
                    reli -= (pc + (rtp - rtofst) + 2)

            if mode & R3_BYTE:
                if mode & R_BYT3:
                    if mode & R_BIT:
                        raise LinkError(
                            "?ASlink-Error-unsupported R_BIT relocation "
                            "mode 0x%03X (%s:%d)" % (mode, path, no))
                    elif mode & R_HIB:
                        relv = self._adb24_sel(reli, rtp, rtval, rtflg, "hi")
                    elif mode & R3_MSB:
                        relv = self._adb24_sel(reli, rtp, rtval, rtflg, "mid")
                    else:
                        relv = self._adb24_sel(reli, rtp, rtval, rtflg, "lo")
                elif mode & R3_BYTX:
                    # two-byte locus, one byte selected (adb_hi/adb_lo):
                    # with hilo=1 the MSB lives at rtp, so selecting the
                    # high byte hides rtp+1 and vice versa.
                    relv = self._adb2(reli, rtp, rtval)
                    if mode & R3_MSB:
                        rtflg[rtp + 1] = 0
                    else:
                        rtflg[rtp] = 0
                else:
                    relv = self._adb1(reli, rtp, rtval)
            elif IS_R_J11(mode):
                relv = self._adb2(reli, rtp, rtval)
                if mode & R_MCS251_CONTROL:
                    if (relv & A_MASK & ~0x7FF) != \
                            ((pc + rtp - rtofst + 2) & A_MASK & ~0x7FF):
                        error = 6
                elif (relv & ~0x7FF) != ((pc + rtp - rtofst) & ~0x7FF):
                    error = 6
                rtval[rtp] = ((rtval[rtp] & 0x07) << 5) | rtval[rtp + 2]
                rtflg[rtp + 2] = 0
                rtofst += 1
            elif IS_R_J19(mode):
                raise LinkError("?ASlink-Error-unsupported J19 relocation "
                                "mode 0x%03X (%s:%d)" % (mode, path, no))
            elif IS_C24(mode):
                relv = self._adb3(reli, rtp, rtval)
            elif IS_R_J16(mode):
                # MCS-251 LCALL/LJMP: PC[15:0] replaced, must stay in the
                # same 64 KiB region as the following instruction.
                relv = self._adb2(reli, rtp, rtval)
                if (relv & 0x00FF0000) != ((pc + rtp - rtofst + 2) & 0x00FF0000):
                    error = 15
            else:
                relv = self._adb2(reli, rtp, rtval)

            if mode & R3_BYTE and mode & R3_BYTX:
                rtofst += (A_BYTES - 1)

            if mode & R3_USGN and mode & R3_BYTE and relv & ~0xFF:
                error = 1
            if mode & R3_PCR and mode & R3_BYTE:
                r = relv & 0xFFFFFF80  # (a_uint)~0x7F
                if r != 0xFFFFFF80 and r != 0:
                    error = 2
            if mode & R3_PAG and (relv & ~0xFF):
                error = 5
            if mode & R_BIT and (relv & ~0x87FF):
                error = 10

            if error:
                errmsg = {
                    1: "Unsigned Byte error",
                    2: "Byte PCR relocation error",
                    5: "Page Mode relocation error",
                    6: "2K Page relocation error",
                    7: "512K Page relocation error",
                    15: "64K Region relocation error",
                    10: "Bit relocation error",
                }.get(error, "relocation error")
                self.err("?ASlink-Relocation Error (%s:%d) area %d mode "
                         "0x%03X: %s" % (path, no, aindex, mode, errmsg))
        # bounds check against the parsed T line (rtval includes the
        # three XH3 address bytes; the narrowest loci need two bytes)
        j = 5
        while j < len(toks):
            mode = int(toks[j], 16)
            j += 1
            if (mode & R_ESCAPE_MASK) == R_ESCAPE_MASK:
                mode = ((mode & ~R_ESCAPE_MASK) << 8) | int(toks[j], 16)
                j += 1
            rtp = int(toks[j], 16)
            j += 3
            need = 3 if ((mode & (R_BYT3 | R3_MSB)) or IS_C24(mode)) else 2
            if rtp < A_BYTES or rtp + need > len(trec.values):
                raise LinkError("?ASlink-Error-R index out of T data "
                                "(%s:%d)" % (path, no))

    # BE add helpers (lkrloc.c adb_1b/adb_2b/adb_3b with hilo=1)
    @staticmethod
    def _be(vals, i, n):
        v = 0
        for k in range(n):
            v = (v << 8) | (vals[i + k] & 0xFF)
        return v

    @staticmethod
    def _adb1(v, i, rtval):
        j = (v + rtval[i]) & 0xFFFFFFFF
        rtval[i] = j & 0xFF
        return j

    @staticmethod
    def _adb2(v, i, rtval):
        j = (v + (rtval[i] << 8) + rtval[i + 1]) & 0xFFFFFFFF
        rtval[i] = (j >> 8) & 0xFF
        rtval[i + 1] = j & 0xFF
        return j

    @staticmethod
    def _adb3(v, i, rtval):
        j = (v + (rtval[i] << 16) + (rtval[i + 1] << 8) + rtval[i + 2]) & 0xFFFFFFFF
        rtval[i] = (j >> 16) & 0xFF
        rtval[i + 1] = (j >> 8) & 0xFF
        rtval[i + 2] = j & 0xFF
        return j

    @staticmethod
    def _adb24_sel(v, i, rtval, rtflg, which):
        """adb_24_lo/mid/hi (lkrloc3.c): add to the 3-byte locus, keep only
        the selected byte for output."""
        j = (v + (rtval[i] << 16) + (rtval[i + 1] << 8) + rtval[i + 2]) & 0xFFFFFFFF
        rtval[i] = (j >> 16) & 0xFF
        rtval[i + 1] = (j >> 8) & 0xFF
        rtval[i + 2] = j & 0xFF
        if which == "lo":
            rtflg[i] = 0      # hilo=1: rtval[i] is the MSB
            rtflg[i + 1] = 0
        elif which == "mid":
            rtflg[i + 2] = 0
            rtflg[i] = 0
        else:  # hi
            rtflg[i + 2] = 0
            rtflg[i + 1] = 0
        return j

    def _emit_trecord(self, trec):
        """ixx()/hexRecord() payload handling: bytes flagged in rtflg are
        appended *sequentially* to the output image starting at areax base
        + T offset.  Bytes hidden by byte-selecting relocations (rtflg=0,
        e.g. mode 0x121) shrink the instruction, exactly like sdld's
        rtbuf[] append loop in ixx()."""
        vals = trec.values
        off = self._be(vals, 0, A_BYTES)
        addr = trec.areax.addr + off
        for k in range(A_BYTES, len(vals)):
            if trec.flags[k]:
                self.image[addr & A_MASK] = vals[k] & 0xFF
                addr += 1

    def _write_ihx(self, out_path):
        """iflush()-style records: <=32 data bytes, type 04 on 64 KiB
        boundary changes, big-endian checksum; end with :00000001FF."""
        if not self.image:
            raise LinkError("?ASlink-Error-no output data")
        addrs = sorted(self.image)
        lines = []
        i = 0
        prev_hi = None
        while i < len(addrs):
            start = addrs[i]
            # extend the record through contiguous bytes, max 32, and
            # never across a 64 KiB boundary (ixx: iflush on &0xffff==0)
            end = min(start + IXXMAXBYTES, (start | 0xFFFF) + 1)
            j = i
            while j < len(addrs) and addrs[j] < end and \
                    (j == i or addrs[j] == addrs[j - 1] + 1):
                j += 1
            chunk = [self.image[a] for a in addrs[i:j]]
            hi = (start >> 16) & 0xFFFF
            if hi != prev_hi:
                chk = (2 + 4 + (hi & 0xFF) + ((hi >> 8) & 0xFF)) & 0xFF
                lines.append(":02000004%04X%02X" % (hi, (-chk) & 0xFF))
                prev_hi = hi
            lo = start & 0xFFFF
            reclen = len(chunk)
            chk = (reclen + (lo & 0xFF) + ((lo >> 8) & 0xFF) +
                   sum(chunk)) & 0xFF
            lines.append(":%02X%04X00%s%02X"
                         % (reclen, lo,
                            "".join("%02X" % b for b in chunk), (-chk) & 0xFF))
            i = j
        lines.append(":00000001FF")
        with open(out_path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# strict ABI signature grammar (lkmain.c:109 mcs251_abi_signature_valid)
# ---------------------------------------------------------------------------

def mcs251_abi_signature_valid(signature):
    fixed = ["stc32-mcs251", "abi-major=1", "abi-minor=0", "target=mcs251",
             None, None, None, None, None, None, None,
             "sdcccall=2", "regset=r0-r9,r12-r15",
             "compiler-build=mcs251-abi1.0-r1"]
    if signature is None:
        return False
    length = len(signature)
    if length == 0 or length > 240 or signature[0] == " " or \
            signature[-1] == " " or "  " in signature:
        return False
    for ch in signature:
        if ch in "\t\r\n":
            return False
    tokens = signature.split(" ")
    if len(tokens) != len(fixed):
        return False
    names = ["stack-auto=", "xstack=", "intlong-reent=",
             "float-reent=", "reg-params=", "all-callee-saves="]
    for i, token in enumerate(tokens):
        if i == 4:
            if token not in ("model=small", "model=large"):
                return False
        elif 5 <= i <= 10:
            pre = names[i - 5]
            if not token.startswith(pre):
                return False
            v = token[len(pre):]
            if len(v) != 1 or v not in "01":
                return False
        elif token != fixed[i]:
            return False
    return True


# ---------------------------------------------------------------------------
# --dump inventory helper
# ---------------------------------------------------------------------------

class RelDump(object):
    def __init__(self, path):
        self.path = path
        self.areas = []
        self.modes = {}
        self.recs = {}
        self.module = None

    def record(self, head, kind, toks, no):
        self.recs[kind] = self.recs.get(kind, 0) + 1
        if kind == "M" and len(toks) > 1:
            self.module = toks[1]
        if kind != "R" or len(toks) < 5:
            return
        aindex = int(toks[3], 16) * 256 + int(toks[4], 16)
        j = 5
        while j < len(toks):
            mode = int(toks[j], 16)
            j += 1
            if (mode & R_ESCAPE_MASK) == R_ESCAPE_MASK:
                mode = ((mode & ~R_ESCAPE_MASK) << 8) | int(toks[j], 16)
                j += 1
            tindex = int(toks[j], 16)
            j += 1
            rindex = int(toks[j], 16) * 256 + int(toks[j + 1], 16)
            j += 2
            key = (mode, aindex, rindex)
            self.modes[key] = self.modes.get(key, 0) + 1

    def report(self):
        print("== %s (module %s)" % (self.path, self.module))
        print("   records: %s" % dict(sorted(self.recs.items())))
        names = [a for a, _, _ in self.areas]
        for (mode, ai, ri), n in sorted(self.modes.items()):
            area = names[ai] if ai < len(names) else "?"
            print("   R mode=0x%03X area=%d(%s) ref=%d  x%d"
                  % (mode, ai, area, ri, n))


# ---------------------------------------------------------------------------
# driver
# ---------------------------------------------------------------------------

def usage():
    sys.stderr.write(
        "usage: mcs251_ld.py -f cmd.lk [--mcs251-abi] [--dump]\n"
        "       mcs251_ld.py cmd.lk ...\n")

def main(argv):
    args = argv[1:]
    strict = "--mcs251-abi" in args
    dump = "--dump" in args
    args = [a for a in args if a not in ("--mcs251-abi", "--dump")]
    lkfile = None
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-f", "--file"):
            i += 1
            if i >= len(args):
                usage()
                return ER_FATAL
            lkfile = args[i]
        elif a.startswith("-f") and len(a) > 2:
            lkfile = a[2:]
        elif a in ("-h", "--help"):
            usage()
            return 0
        elif lkfile is None and not a.startswith("-"):
            lkfile = a
        elif a.startswith("-"):
            sys.stderr.write("?ASlink-Warning-Unkown option %s ignored\n" % a)
        i += 1
    if lkfile is None:
        usage()
        return ER_FATAL

    lnk = Linker(strict_abi=strict, dump=dump)
    try:
        lnk.parse_command_file(lkfile)
        if dump:
            lnk.read_all_rels()
            return 0
        lnk.read_all_rels()
        lnk.check_abi_modules()
        if lnk.strict_abi and lnk.lkerr:
            return ER_ERROR
        lnk.setarea()
        lnk.lnkarea2()
        lnk.symdef()
        if lnk.lkerr:
            return ER_ERROR
        out = lnk.output_stem + ".hex"
        lnk.relocate_and_output(out)
        if lnk.lkerr:
            return ER_ERROR
        sys.stderr.write("linked %d area(s), %d byte image -> %s\n"
                         % (len(lnk.areas), len(lnk.image), out))
        return 0
    except LinkError as e:
        sys.stderr.write(str(e) + "\n")
        return ER_FATAL


if __name__ == "__main__":
    sys.exit(main(sys.argv))
