#!/usr/bin/env python3
"""Structural companion checker for the MCS-251 fixed-bit tests.

Used by the RUN lines of clang/test/CodeGen/mcs251-bit-fixed-ref.c and
clang/test/PCH/mcs251-bit.c.  Every RUN line that invokes this script pipes
the emitted module through ``opt -passes=verify -S`` first, so the input is
ASSUMED TO BE VERIFIER-CLEAN LLVM IR: illegal modules (unknown opcodes,
nonexistent successors, malformed instructions) are killed by opt before
this script ever runs.  The script itself is a finite closed-world parser:
any instruction form it does not recognize makes the whole check FAIL
(fail-closed) instead of being silently ignored.

  S1  every basic block of every function is well formed: it has exactly one
      terminator and that terminator is the block's last instruction (an arm
      "interrupted" by an early ret/br, or an unterminated arm, is rejected);
  S2  any basic block containing a call to an llvm.mcs251.bit.* intrinsic is
      reachable from its function's entry block, by following br/switch/
      indirectbr/invoke/callbr successor edges (writes parked behind a dead
      label are rejected even though their textual order is unchanged);
  S3  a function that has the conditional-write lowering owns the blocks
      mcs251.bit.set / mcs251.bit.clear / mcs251.bit.cont; each arm block
      contains exactly one bit write -- set in the set arm, clear in the
      clear arm -- ends in `br label %mcs251.bit.cont` (trailing metadata
      attachments tolerated), is reachable and has at least one predecessor;
  S4  when a function returns a value loaded from memory, the loaded slot is
      captured by a conservative alias closure over the finite set of
      pointer-producing instruction forms: an alloca itself, a GEP of any
      base and any offsets (dynamic offsets collapse to "may alias"),
      bitcast/ptrcast/addrspacecast, select (aliasing if ANY arm aliases),
      phi (aliasing if ANY incoming aliases) and freeze.  Every memory write
      in the function is then resolved through the same closure, where the
      write set is a finite enumeration: store, atomicrmw, atomiccmpxchg,
      calls to llvm.memcpy/memmove/memset.*, va_arg (which advances its
      in-memory list), and ANY other call except the four exact names
      llvm.mcs251.bit.read/set/clear/toggle. Unknown widths are may-alias,
      never zero-byte writes; scalar integer widths include i1 and i128.
      The captured slot must have exactly one aliasing store -- byte-interval overlap,
      possibly-aliasing accesses are rejected conservatively -- placed
      earlier in the same block, and no other write-set member may touch it;
  S5  with --mode=pch (the PCH payload TU) the module must define exactly
      one function, so an extra spliced-in function cannot carry the writes.

Quoted identifiers (@\"name\" / %\"name\", including \\XX escapes) are parsed
as ordinary names, so quoted labels, quoted function names and quoted local
values cannot hide structure from S1-S5.  Multi-line instructions (a switch
or callbr whose successor list continues over several lines) are joined
before classification.  Exit status 0 means every property holds; 1 means a
violation (or an unrecognized instruction form), with a diagnostic on
stderr.  Only the Python standard library is used, and the input is parsed
at the textual .ll level (no LLVM bindings).
"""

import argparse
import re
import sys

BIT_INTRIN = "@llvm.mcs251.bit."
BARE = r"[A-Za-z0-9_.$\-]+"
QUOTED = r'"(?:[^"\\]|\\.)*"'
LOC = r"%%(?:%s|%s)" % (BARE, QUOTED)
GLOB = r"@(?:%s|%s)" % (BARE, QUOTED)
PTOK = r"(?:%%(?:%s|%s)|@(?:%s|%s)|[^,\s]+)" % (BARE, QUOTED, BARE, QUOTED)
# Optional address-space qualifier on a pointer operand ("ptr addrspace(1) %x").
ASQ = r"(?:\s+addrspace\(\d+\))?"
# A pointer operand position: "ptr" [addrspace(N)] <token>.
PPTR = r"ptr" + ASQ + r"\s+(%s)"

LABEL_RE = re.compile(r"^((?:%s|%s)):(?:\s|$)" % (BARE, QUOTED))
DEFINE_RE = re.compile(r"^define\b.*?(%s)\s*\(" % GLOB)
ASSIGN_RE = re.compile(r"^(%s)\s*=\s*(.*)$" % LOC, re.S)
LABEL_REF_RE = re.compile(r"label\s+(%s)" % LOC)
RET_REG_RE = re.compile(r"^ret\s+\S+\s+(%s)\s*$" % LOC)
CALL_MOD_RE = re.compile(r"^(musttail|notail|tail)(?=\s+call\b)")
OPCODE_RE = re.compile(r"^[A-Za-z][A-Za-z0-9._]*")
META_TAIL_RE = re.compile(r"(?:\s*,\s*![A-Za-z0-9_.$]+\s+![0-9]+)+\s*$")
STORE_RE = re.compile(
    r"^store\s+(?:atomic\s+)?(?:volatile\s+)?([^\s,]+)\s+[^,]+,\s*" + PPTR % PTOK
)
LOAD_RE = re.compile(
    r"^load\s+(?:atomic\s+)?(?:volatile\s+)?([^\s,]+)\s*,\s*" + PPTR % PTOK
)
GEP_RE = re.compile(
    r"^getelementptr\s+(?:inbounds\s+|nuw\s+|nusw\s+)*([^\s,]+)\s*,\s*" + PPTR % PTOK
    + r"\s*(?:,\s*(.*))?$",
    re.S,
)
TRANSPARENT_RE = re.compile(
    r"^(?:bitcast|addrspacecast|ptrcast)\s+" + PPTR % PTOK + r"\s+to\s"
)
FREEZE_RE = re.compile(
    r"^freeze\s+(?:<[^>]*>\s*)?" + PPTR % PTOK + r"(?:\s|,|$)"
)
SELECT_RE = re.compile(
    r"^select\s+[^,]+,\s*(?:<[^>]*>\s*)?" + PPTR % PTOK
    + r"\s*,\s*(?:<[^>]*>\s*)?" + PPTR % PTOK
)
PHI_RE = re.compile(r"^phi\s+(?:<[^>]*>\s*)?ptr" + ASQ + r"\s+(\[.*)$", re.S)
PHI_VAL_RE = re.compile(r"\[\s*(%s)\s*," % PTOK)
ALLOCA_RE = re.compile(r"^alloca\b")
ATOMICRMW_RE = re.compile(
    r"^atomicrmw\s+(?:volatile\s+)?[a-z_]+\s+" + PPTR % PTOK + r"\s*,\s*([^\s,]+)"
)
CMPXCHG_RE = re.compile(
    r"^(?:atomic)?cmpxchg\s+(?:weak\s+)?(?:volatile\s+)?\s*" + PPTR % PTOK
    + r"\s*,\s*([^\s,]+)"
)
CALLEE_RE = re.compile(r"(%s)\s*\(" % GLOB)
PTR_OPERAND_RE = re.compile(PPTR % PTOK)
SIZE = {"i8": 1, "i16": 2, "i32": 4, "i64": 8, "float": 4, "double": 8}

# The closed world of instruction forms this checker understands.  Anything
# else inside a function body is a FAIL (fail-closed): a shape the checker
# cannot interpret must never be treated as "no violation found".
TERMINATORS = {
    "ret", "br", "switch", "indirectbr", "invoke", "callbr", "resume",
    "catchswitch", "catchret", "cleanupret", "unreachable",
}
ASSIGNABLE_TERMINATORS = {"invoke", "callbr", "catchswitch"}
KNOWN_OPS = TERMINATORS | {
    "add", "fadd", "sub", "fsub", "mul", "fmul", "udiv", "sdiv", "fdiv",
    "urem", "srem", "frem", "shl", "lshr", "ashr", "and", "or", "xor",
    "icmp", "fcmp", "phi", "select", "freeze", "call", "va_arg", "alloca",
    "load", "store", "fence", "atomiccmpxchg", "cmpxchg", "atomicrmw",
    "getelementptr",
    "trunc", "zext", "sext", "fptrunc", "fpext", "fptoui", "fptosi",
    "uitofp", "sitofp", "ptrtoint", "inttoptr", "bitcast", "addrspacecast",
    "ptrcast", "landingpad", "catchpad", "cleanuppad",
}
PINNED_BIT_CALLEES = frozenset(
    "llvm.mcs251.bit." + name for name in ("read", "set", "clear", "toggle")
)
WRITE_CALL_PREFIXES = ("llvm.memcpy.", "llvm.memmove.", "llvm.memset.")
WILDCARD = None  # a pointer that may denote anything


def access_size(ty):
    """Scalar byte width, or unknown. Never model an unknown width as zero."""
    if re.fullmatch(r"i[1-9][0-9]*", ty):
        return (int(ty[1:]) + 7) // 8
    return SIZE.get(ty)


class Fail(Exception):
    pass


def norm_ident(tok):
    """Normalize an identifier token (@x / %x / @"q" / %"q") to its plain
    name, unescaping \\XX, \\" and \\\\ inside quoted forms."""
    if tok[:1] in "@%":
        tok = tok[1:]
    if tok[:1] != '"':
        return tok
    body, out, i = tok[1:-1], [], 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 2 < len(body) and \
                body[i + 1] in "0123456789abcdefABCDEF" and \
                body[i + 2] in "0123456789abcdefABCDEF":
            out.append(chr(int(body[i + 1:i + 3], 16)))
            i += 3
        elif c == "\\" and i + 1 < len(body) and body[i + 1] in '"\\':
            out.append(body[i + 1])
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


class Inst:
    """One logical instruction (multi-line constructs pre-joined)."""

    def __init__(self, line_no, text):
        self.line_no = line_no
        self.text = text
        m = ASSIGN_RE.match(text)
        if m:
            self.name = norm_ident(m.group(1))
            rhs = m.group(2).strip()
        else:
            self.name = None
            rhs = text.strip()
        mm = CALL_MOD_RE.match(rhs)
        if mm:
            rhs = rhs[mm.end():].lstrip()
        om = OPCODE_RE.match(rhs)
        self.opcode = om.group(0) if om else ""
        self.rhs = rhs

    def meta_stripped(self):
        return META_TAIL_RE.sub("", self.text)


class Block:
    def __init__(self, name):
        self.name = name
        self.insts = []  # Inst objects

    @property
    def text(self):
        return "\n".join(i.text for i in self.insts)


class Function:
    def __init__(self, name, line_no):
        self.name = name
        self.line_no = line_no
        self.blocks = []
        self.by_name = {}

    def add(self, block):
        self.blocks.append(block)
        if block.name in self.by_name:
            raise Fail(
                "function @%s (line %d): duplicate block label %r"
                % (self.name, self.line_no, block.name)
            )
        self.by_name[block.name] = block

    @property
    def entry(self):
        return self.blocks[0]


def strip_comment(line):
    out = []
    in_str = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if c == "\\" and i + 1 < len(line):
                out.append(line[i : i + 2])
                i += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == ";":
                break
        out.append(c)
        i += 1
    return "".join(out).rstrip()


INVOKE_TAIL_RE = re.compile(r"\bto\s+label\b")
LANDING_CLAUSE_RE = re.compile(r"\b(cleanup|catch|filter)\b")


def inst_complete(text):
    """True when `text` is a structurally complete instruction.  Besides the
    bracket-balance rule (multi-line switch/callbr case lists), the LLVM
    printer may wrap an invoke/callbr onto a following `to label ...` line
    and a landingpad onto a following clause line; those continuations are
    joined before classification."""
    if bracket_depth(text) != 0:
        return False
    rhs = text.strip()
    m = ASSIGN_RE.match(rhs)
    if m:
        rhs = m.group(2).strip()
    mm = CALL_MOD_RE.match(rhs)
    if mm:
        rhs = rhs[mm.end():].lstrip()
    om = OPCODE_RE.match(rhs)
    op = om.group(0) if om else ""
    if op == "switch":
        # The default destination and even the opening case-list bracket may
        # start on the next physical line.
        return bool(re.search(r"\blabel\s+" + LOC + r"\s*\[", rhs))
    if op in ("invoke", "callbr"):
        return bool(INVOKE_TAIL_RE.search(rhs))
    if op == "landingpad":
        return bool(LANDING_CLAUSE_RE.search(rhs))
    return True


def bracket_depth(text):
    # Keep delimiter kinds, not only a net depth: ([)] must not look balanced.
    stack = []
    in_str = False
    i = 0
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\" and i + 1 < len(text):
                i += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c in "[({":
                stack.append(c)
            elif c in "])}":
                matching = {"]": "[", ")": "(", "}": "{"}
                if not stack or stack.pop() != matching[c]:
                    raise Fail("mismatched instruction delimiters")
        i += 1
    return len(stack)


def parse_functions(text):
    funcs = []
    cur = None
    block = None
    pending = []  # physical lines of an instruction that continues over lines

    def flush():
        nonlocal pending
        if pending:
            inst = Inst(pending[0][0], " ".join(t for _, t in pending))
            if inst.opcode not in KNOWN_OPS:
                raise Fail(
                    "function @%s (line %d): unrecognized instruction form "
                    "%r (fail-closed: the checker only accepts a finite "
                    "enumeration of LLVM instruction shapes)"
                    % (cur.name, inst.line_no, inst.rhs.split("(")[0].strip())
                )
            if (inst.opcode in TERMINATORS
                    and inst.opcode not in ASSIGNABLE_TERMINATORS
                    and inst.name is not None):
                raise Fail(
                    "function @%s (line %d): terminator %r cannot produce a "
                    "value" % (cur.name, inst.line_no, inst.opcode)
                )
            if block is None:
                raise Fail(
                    "function @%s (line %d): instruction outside any block"
                    % (cur.name, cur.line_no)
                )
            block.insts.append(inst)
            pending = []

    for no, raw in enumerate(text.splitlines(), 1):
        line = strip_comment(raw).strip()
        if not line:
            continue
        if cur is None:
            m = DEFINE_RE.match(line)
            if m:
                cur = Function(norm_ident(m.group(1)), no)
                block = None
            continue
        if pending:
            pending.append((no, line))
            if inst_complete(" ".join(t for _, t in pending)):
                flush()
            continue
        if line == "}":
            flush()
            if not cur.blocks:
                raise Fail(
                    "function @%s (line %d): no basic blocks" % (cur.name, cur.line_no)
                )
            funcs.append(cur)
            cur = None
            block = None
            continue
        m = LABEL_RE.match(line)
        if m:
            block = Block(norm_ident(m.group(1)))
            cur.add(block)
            continue
        if block is None:
            block = Block(cur.blocks[0].name if cur.blocks else "entry")
            if cur.blocks:
                raise Fail(
                    "function @%s (line %d): instruction before first label"
                    % (cur.name, cur.line_no)
                )
            cur.add(block)
        pending = [(no, line)]
        if inst_complete(line):
            flush()
    if cur is not None:
        raise Fail("function @%s (line %d): unterminated (missing '}')" % (cur.name, cur.line_no))
    return funcs


def successors(block):
    if not block.insts:
        return []
    return [norm_ident(s) for s in LABEL_REF_RE.findall(block.insts[-1].text)]


def check_well_formed(fn):
    for b in fn.blocks:
        term = [i for i, inst in enumerate(b.insts) if inst.opcode in TERMINATORS]
        if not term:
            raise Fail(
                "function @%s: block %r has no terminator" % (fn.name, b.name)
            )
        if term != [len(b.insts) - 1]:
            raise Fail(
                "function @%s: block %r is interrupted by a terminator at "
                "instruction %d of %d (write chain not in one basic block)"
                % (fn.name, b.name, term[0] + 1, len(b.insts))
            )


def reachable_from_entry(fn):
    seen = set()
    work = [fn.entry.name]
    while work:
        n = work.pop()
        if n in seen or n not in fn.by_name:
            continue
        seen.add(n)
        work.extend(successors(fn.by_name[n]))
    return seen


def check_bit_calls_reachable(fn):
    reach = reachable_from_entry(fn)
    for b in fn.blocks:
        if BIT_INTRIN in b.text and b.name not in reach:
            raise Fail(
                "function @%s: block %r contains a bit intrinsic call but is "
                "not reachable from entry" % (fn.name, b.name)
            )
    return reach


ARMS = (
    ("mcs251.bit.set", "@llvm.mcs251.bit.set("),
    ("mcs251.bit.clear", "@llvm.mcs251.bit.clear("),
)


def check_arms(fn, reach):
    present = [a for a, _ in ARMS if a in fn.by_name]
    if not present:
        return
    if len(present) != len(ARMS):
        raise Fail(
            "function @%s: has arm block %r but not both arms" % (fn.name, present[0])
        )
    cont = "mcs251.bit.cont"
    if cont not in fn.by_name:
        raise Fail("function @%s: no %r join block" % (fn.name, cont))
    preds = {b.name: 0 for b in fn.blocks}
    for b in fn.blocks:
        for s in successors(b):
            if s in preds:
                preds[s] += 1
    for arm, callee in ARMS:
        b = fn.by_name[arm]
        calls = [t.text for t in b.insts if BIT_INTRIN in t.text]
        if len(calls) != 1 or callee not in calls[0]:
            raise Fail(
                "function @%s: arm block %r must contain exactly one %s...) "
                "write; found %d bit call(s) in that block"
                % (fn.name, arm, callee, len(calls))
            )
        tm = re.match(r"^br\s+label\s+(%s)\s*$" % LOC, b.insts[-1].meta_stripped())
        if not tm or norm_ident(tm.group(1)) != cont:
            raise Fail(
                "function @%s: arm block %r must end in "
                "'br label %%mcs251.bit.cont'; ends in %r" % (fn.name, arm, b.insts[-1].text)
            )
        if b.name not in reach:
            raise Fail("function @%s: arm block %r is unreachable" % (fn.name, arm))
        if preds[b.name] == 0:
            raise Fail("function @%s: arm block %r has no predecessor" % (fn.name, arm))
    if fn.by_name[cont].name not in reach:
        raise Fail("function @%s: join block %r is unreachable" % (fn.name, cont))


def defs_of(fn):
    d = {}
    for b in fn.blocks:
        for inst in b.insts:
            if inst.name is None:
                continue
            if inst.name in d:
                raise Fail(
                    "function @%s (line %d): multiple definitions of %%%s "
                    "(not valid SSA)" % (fn.name, inst.line_no, inst.name)
                )
            d[inst.name] = inst
    return d


def resolve_ptr_set(defs, tok, depth=0):
    """Conservative alias closure: the set of (base, byte_offset, exact)
    possibilities a pointer token may denote.  base WILDCARD (None) marks a
    pointer the closure cannot see through (call/load results): it may
    denote anything.  Falls back to WILDCARD past the recursion guard."""
    if depth > 64:
        return {(WILDCARD, 0, False)}
    name = norm_ident(tok)
    inst = defs.get(name)
    if inst is None:
        # Function argument, global, or an unnamed base: a base object.
        return {(name, 0, True)}
    rhs = inst.rhs
    m = TRANSPARENT_RE.match(rhs)
    if m:
        return resolve_ptr_set(defs, m.group(1), depth + 1)
    m = FREEZE_RE.match(rhs)
    if m:
        return resolve_ptr_set(defs, m.group(1), depth + 1)
    m = SELECT_RE.match(rhs)
    if m:
        out = set()
        for arm in (m.group(1), m.group(2)):
            out |= resolve_ptr_set(defs, arm, depth + 1)
        return out
    m = PHI_RE.match(rhs)
    if m:
        out = set()
        for val in PHI_VAL_RE.findall(m.group(1)):
            out |= resolve_ptr_set(defs, val, depth + 1)
        return out or {(WILDCARD, 0, False)}
    m = GEP_RE.match(rhs)
    if m:
        ty, base, rest = m.group(1), m.group(2), m.group(3) or ""
        out = set()
        for b, boff, bex in resolve_ptr_set(defs, base, depth + 1):
            off, exact = boff, bex
            if ty not in SIZE:
                exact = False
            for piece in rest.split(","):
                piece = piece.strip()
                if not piece:
                    continue
                im = re.match(r"^[iu]\d+\s+(-?\d+)$", piece)
                if not im:
                    exact = False  # dynamic (register) index: may alias
                    continue
                off += int(im.group(1)) * SIZE.get(ty, 0)
            out.add((b, off, exact))
        return out
    if ALLOCA_RE.match(rhs):
        return {(name, 0, True)}
    # Value produced by a call/load/computation: opaque under the closure;
    # conservatively it may denote anything.
    return {(WILDCARD, 0, False)}


def access_hits(cands, asize, slotset, unbounded=False):
    """Classify an access of `asize` bytes through `cands` against the
    result-slot possibilities.  Returns (exact_hit, fuzzy_hit).  With
    `unbounded` (write calls of unknown length) any resolution onto the
    slot's base counts as a possible hit."""
    exact_hit = False
    fuzzy_hit = False
    for base, off, exact in cands:
        if base is WILDCARD:
            fuzzy_hit = True
            continue
        for sbase, slo, shi, sexact in slotset:
            if sbase is WILDCARD:
                fuzzy_hit = True
                continue
            if base != sbase:
                continue
            if not (exact and sexact):
                fuzzy_hit = True
                continue
            if unbounded or asize is None:
                fuzzy_hit = True
            elif off < shi and slo < off + asize:
                exact_hit = True
    return exact_hit, fuzzy_hit


def callee_of(inst):
    m = CALLEE_RE.search(inst.rhs)
    if not m:
        return None
    return norm_ident(m.group(1))


def check_result_slot(fn, reach):
    rets = [t for b in fn.blocks for t in b.insts if t.opcode == "ret"]
    if not rets:
        return
    m = RET_REG_RE.match(rets[0].meta_stripped())
    if not m:
        return  # constant return: no memory chain to check
    reg = norm_ident(m.group(1))
    defs = defs_of(fn)
    d = defs.get(reg)
    if d is None:
        raise Fail("function @%s: ret operand %%%s has no def" % (fn.name, reg))
    if d.opcode != "load":
        return  # zext/canonical value: no load to protect
    lm = LOAD_RE.match(d.rhs)
    if not lm:
        raise Fail(
            "function @%s: result load at line %d has an unrecognized form"
            % (fn.name, d.line_no)
        )
    lty, lptr = lm.group(1), lm.group(2)
    lblock = None
    for b in fn.blocks:
        for t in b.insts:
            if t is d:
                lblock = b
    if lblock is None or lblock.name not in reach:
        raise Fail(
            "function @%s: result load is in unreachable block %r"
            % (fn.name, "?" if lblock is None else lblock.name)
        )
    lsize = access_size(lty)
    if lsize is None:
        raise Fail("function @%s: unknown result load width %s" % (fn.name, lty))
    slotset = {(b, o, o + lsize, e)
               for (b, o, e) in resolve_ptr_set(defs, lptr)}
    aliasing = []
    for b in fn.blocks:
        for t in b.insts:
            if t.opcode == "store":
                sm = STORE_RE.match(t.rhs)
                if not sm:
                    raise Fail(
                        "function @%s: store at line %d has an unrecognized "
                        "form" % (fn.name, t.line_no)
                    )
                sty, tgt = sm.group(1), sm.group(2)
                ssize = access_size(sty)
                cands = resolve_ptr_set(defs, tgt)
                exact_hit, fuzzy_hit = access_hits(cands, ssize, slotset)
                if not (exact_hit or fuzzy_hit):
                    continue
                if fuzzy_hit or ssize is None:
                    raise Fail(
                        "function @%s: memory write at line %d may alias the "
                        "result slot (%s resolves outside the alias closure)"
                        % (fn.name, t.line_no, tgt)
                    )
                aliasing.append((b, t))
            elif t.opcode in ("atomicrmw", "atomiccmpxchg", "cmpxchg"):
                am = (ATOMICRMW_RE.match(t.rhs) if t.opcode == "atomicrmw"
                      else CMPXCHG_RE.match(t.rhs))
                if not am:
                    raise Fail(
                        "function @%s: %s at line %d has an unrecognized form"
                        % (fn.name, t.opcode, t.line_no)
                    )
                tgt, ty = am.group(1), am.group(2)
                cands = resolve_ptr_set(defs, tgt)
                exact_hit, fuzzy_hit = access_hits(cands, access_size(ty), slotset)
                if exact_hit or fuzzy_hit:
                    raise Fail(
                        "function @%s: %s at line %d writes the result slot "
                        "through the alias closure" % (fn.name, t.opcode, t.line_no)
                    )
            elif t.opcode == "va_arg":
                # va_arg advances the va_list in memory; it is not just a load.
                vm = re.match(r"^va_arg\s+" + PPTR % PTOK, t.rhs)
                if not vm:
                    raise Fail("function @%s: unrecognized va_arg" % fn.name)
                hits = access_hits(resolve_ptr_set(defs, vm.group(1)), None,
                                   slotset, unbounded=True)
                if any(hits):
                    raise Fail("function @%s: va_arg may write the result slot"
                               % fn.name)
            elif t.opcode in ("call", "invoke", "callbr"):
                callee = callee_of(t)
                if callee is None:
                    raise Fail(
                        "function @%s: call at line %d has no parseable "
                        "callee" % (fn.name, t.line_no)
                    )
                if callee in PINNED_BIT_CALLEES:
                    continue  # the pinned bit intrinsics: not memory writes
                if callee.startswith(WRITE_CALL_PREFIXES):
                    rest = t.rhs[t.rhs.index("(", t.rhs.find("@")) + 1:]
                    dm = PTR_OPERAND_RE.search(rest)
                    if not dm:
                        raise Fail(
                            "function @%s: %s call at line %d has no parseable "
                            "destination" % (fn.name, callee, t.line_no)
                        )
                    cands = resolve_ptr_set(defs, dm.group(1))
                    exact_hit, fuzzy_hit = access_hits(
                        cands, None, slotset, unbounded=True)
                    if exact_hit or fuzzy_hit:
                        raise Fail(
                            "function @%s: %s at line %d writes through the "
                            "alias closure of the result slot"
                            % (fn.name, callee, t.line_no)
                        )
                    continue
                raise Fail(
                    "function @%s: call to @%s at line %d inside the "
                    "result-slot window: only the pinned llvm.mcs251.bit.* "
                    "intrinsics are known not to clobber the result slot "
                    "(fail-closed)" % (fn.name, callee, t.line_no)
                )
    if len(aliasing) != 1:
        raise Fail(
            "function @%s: result slot has %d aliasing store(s), "
            "expected exactly 1: %s"
            % (fn.name, len(aliasing), [t.text for _, t in aliasing])
        )
    sb, st = aliasing[0]
    if sb.name != lblock.name or not st.line_no < d.line_no:
        raise Fail(
            "function @%s: the single aliasing store (block %r line %d) is "
            "not earlier in the result load's block (%r line %d)"
            % (fn.name, sb.name, st.line_no, lblock.name, d.line_no)
        )


def run(text, mode):
    funcs = parse_functions(text)
    if not funcs:
        raise Fail("no function definitions found in input")
    bit_funcs = [f for f in funcs if BIT_INTRIN in "\n".join(
        t.text for b in f.blocks for t in b.insts)]
    if not bit_funcs:
        raise Fail("no llvm.mcs251.bit.* call found in any function")
    for fn in funcs:
        check_well_formed(fn)
        reach = check_bit_calls_reachable(fn)
        check_arms(fn, reach)
        check_result_slot(fn, reach)
    if mode == "pch" and len(funcs) != 1:
        raise Fail(
            "--mode=pch: the payload TU must define exactly one function; "
            "found %d: %s" % (len(funcs), [f.name for f in funcs])
        )
    return len(funcs)


def regression_cases():
    """Verifier-clean positive/negative pairs for M2-24..28 and va_arg."""
    pre = "declare void @llvm.mcs251.bit.set(i32)\n"
    def module(body, extra=""):
        return pre + extra + "define i32 @probe(i1 %cond) {\n" + body + "\n}\n"
    entry = ("entry:\n  %r = alloca i128, align 16\n"
             "  %other = alloca i128, align 16\n"
             "  call void @llvm.mcs251.bit.set(i32 128)\n")
    end = "  store i32 1, ptr %r\n  %v = load i32, ptr %r\n  ret i32 %v"
    for op, template in (
        ("atomic128", "  %a = atomicrmw xchg ptr TARGET, i128 0 monotonic, align 16\n"),
        ("cmpxchg128", "  %a = cmpxchg ptr TARGET, i128 0, i128 1 monotonic monotonic, align 16\n"),
        ("store-i1", "  %p = freeze ptr TARGET\n  store i1 false, ptr %p\n"),
        ("unknown-width", "  store ptr null, ptr TARGET\n"),
        ("va-arg", "  %a = va_arg ptr TARGET, i32\n"),
    ):
        for target, want in (("%r", 1), ("%other", 0)):
            yield op + target, module(entry + template.replace("TARGET", target) + end), want
    yield "pinned-call", module(entry + end), 0
    yield "unlisted-call", module(
        entry + '  call void @"llvm.mcs251.bit.eraser"(ptr %r)\n' + end,
        'declare void @"llvm.mcs251.bit.eraser"(ptr)\n'), 1
    for target, want in (("%r", 1), ("%other", 0)):
        chain = ("  br i1 %cond, label %left, label %right\n"
                 "left:\n  br label %join\nright:\n  br label %join\njoin:\n"
                 "  %p = phi ptr [ TARGET, %left ], [ TARGET, %right ]\n"
                 "  %as = addrspacecast ptr %p to ptr addrspace(1)\n"
                 "  %back = addrspacecast ptr addrspace(1) %as to ptr\n"
                 "  %fr = freeze ptr %back\n  store i32 0, ptr %fr\n")
        yield "phi" + target, module(entry + chain.replace("TARGET", target) + end), want
    for offset, want in ((8, 0), (5, 1)):
        body = (entry + "  %slot = getelementptr i8, ptr %r, i32 4\n" +
                f"  %adj = getelementptr i8, ptr %r, i32 {offset}\n" +
                "  store i32 1, ptr %slot\n  store i8 0, ptr %adj\n"
                "  %v = load i32, ptr %slot\n  ret i32 %v")
        yield "offset%d" % offset, module(body), want
    for dead in (False, True):
        body = ("entry:\n  switch i1 %cond,\n    label %done [\n"
                "    i1 0, label %TARGET\n  ]\nwrite:\n"
                "  call void @llvm.mcs251.bit.set(i32 128)\n"
                "  br label %done\ndone:\n  ret i32 0")
        yield "switch%d" % dead, module(body.replace("TARGET", "done" if dead else "write")), int(dead)
    for dead in (False, True):
        body = ("entry:\n  call void @helper(\n"
                "    i32 add (i32 ptrtoint (ptr @g to i32), i32 1))\n"
                "  br label %TARGET\nwrite:\n"
                "  call void @llvm.mcs251.bit.set(i32 128)\n"
                "  br label %done\ndone:\n  ret i32 0")
        yield "paren%d" % dead, module(body.replace("TARGET", "done" if dead else "write"),
                                       "@g = external global i32\ndeclare void @helper(i32)\n"), int(dead)


def self_test(opt):
    import subprocess
    count = 0
    for name, text, want in regression_cases():
        verified = subprocess.run([opt, "-passes=verify", "-S", "-"],
                                  input=text, text=True, capture_output=True)
        if verified.returncode:
            raise Fail("self-test %s: verifier rejected fixture: %s"
                       % (name, verified.stderr))
        for form, data in (("raw", text), ("canonical/chain", verified.stdout)):
            try:
                run(data, "auto")
                got = 0
            except Fail:
                got = 1
            if got != want:
                raise Fail("self-test %s %s: rc=%d, expected %d"
                           % (name, form, got, want))
        count += 1
    print("mcs251-bit-structure-check: %d bidirectional regression cases PASS" % count)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true",
                    help="run embedded bidirectional checker regressions")
    ap.add_argument("--opt", default="opt", help="verifier for --self-test")
    ap.add_argument("input", nargs="?", help=".ll file to check (default: stdin)")
    ap.add_argument("--mode", choices=["auto", "pch"], default="auto")
    args = ap.parse_args()
    try:
        if args.self_test:
            self_test(args.opt)
        else:
            data = (open(args.input, "r", errors="replace").read()
                    if args.input else sys.stdin.read())
            run(data, args.mode)
    except Fail as e:
        sys.stderr.write("mcs251-bit-structure-check: FAIL: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
