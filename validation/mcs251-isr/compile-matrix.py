#!/usr/bin/env python3
# compile-matrix.py - ISR campaign T09 integration matrix (ISR-TASK-BREAKDOWN.md
# T09 card). Drives the whole C -> IR -> ELF chain with frozen tool identities:
#
#   clang (s1)   C front end,   -mcs251-memory-contract=1,1,32,8,1
#   opt   (s1)   GlobalDCE/IPO retention proof
#   llc   (s1)   object emission, -mcs251-memory-contract=1,1,32,8,1
#   mcs251-lld   frozen standalone lld (isr-lld tree)
#   yaml2obj / llvm-readobj  frozen isr-link tree (CRT build + object probes)
#
# Frozen interface (card step 2):
#   compile-matrix.py --clang ABS --llc ABS --opt ABS --lld ABS \
#     --yaml2obj ABS --readobj ABS --out-dir ABS
#
# Per card step 3 the C optimization matrix is O0/O1/O2/O3/Os over the eight
# case families selected inside firmware.c (see its header comment). Per case:
#   - IR checks: ISR keeps CC + canonical slot attribute + llvm.used root;
#     helpers carry no CC128 and no slot attribute (card steps 4/6),
#   - opt GlobalDCE/IPO retention re-check on the optimized IR (card step 5),
#   - object checks: .mcs251.isr carries exactly 24B x 2N paired
#     ENTRY/REGISTER records with one zero-width type9 association each
#     (card step 4); helper objects carry no ISR metadata,
#   - final-assembly evidence (array/spill/all cases): llc re-runs with
#     -filetype=asm into <out>/asm and the script independently asserts
#     the machine code actually carries the coverage the case names
#     claim -- isr_array: >= 8 bytes of inc-spx local frame plus >= 2
#     deep @spx-relative array-element accesses; isr_spill: exactly 10
#     noinline ecall boundaries plus >= 5 stack-slot spill stores,
#     >= 5 spill loads and >= 4 distinct frame displacements. A combo
#     whose final assembly lacks the evidence FAILs (review finding: the
#     old array/spill bodies optimized the claimed frames/spills away),
#   - link with the new CRT (crt-irq.yaml, BOOT=0xFF0210, card step 5),
#   - image verification: the R4 acceptance basis is this script's own
#     independent PT_LOAD-level check (39 EJMP formula, reserved holes,
#     frozen default word, exact registered-handler targets = firmware
#     object symtab st_value + the firmware object's own .text row base
#     from the map, 3-byte reset into BOOT). Per the T09 review finding 2
#     Track A ruling, the frozen T07 isr-check-image.py is still run
#     verbatim on every linked image but recorded as informational only:
#     its HANDLER_STRIDE=41 expectation is a lit-fixture convention that
#     real firmware does not satisfy, and its frozen duty stays in the
#     lld lit suite,
#   - QEMU-PENDING runtime expectations for globals and bit-byte init
#     (card step 6: the run itself belongs to T10; this script freezes the
#     images and the expected bytes),
#   - JSON with commands, versions, sha256, results (card step 8). No stack
#     safety fields of any kind are emitted.
#
# Negative cases (card step 7) must die on the frozen diagnostics and never
# produce an object.

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
FIRMWARE_C = os.path.join(SCRIPT_DIR, "firmware.c")
HELPER_C = os.path.join(SCRIPT_DIR, "helper.c")
CRT_YAML = os.path.join(REPO, "validation", "mcs251-elf", "runtime",
                        "crt-irq.yaml")
CHECK_CRT_IRQ = os.path.join(REPO, "validation", "mcs251-elf", "runtime",
                             "check-crt-irq.py")
T07_CHECK_IMAGE = os.path.join(REPO, "lld", "test", "MCS251", "Inputs",
                               "isr-check-image.py")

OPT_LEVELS = ["O0", "O1", "O2", "O3", "Os"]

# Case family -> (slots, needs -fmcs251-keil, needs helper.o, isr symbols).
CASES = {
    "gnu":      ([1], False, False, [("isr_gnu", 1)]),
    "keil":     ([2], True,  False, [("isr_keil", 2)]),
    "internal": ([3], False, False, [("isr_internal", 3)]),
    "early":    ([4], False, True,  [("isr_early", 4)]),
    "array":    ([5], False, False, [("isr_array", 5)]),
    "spill":    ([6], False, False, [("isr_spill", 6)]),
    "xtu":      ([8], False, True,  [("isr_xtu", 8)]),
    "multi":    ([9], False, True,  [("isr_multi", 9)]),
    "all":      ([1, 2, 3, 4, 5, 6, 8, 9], True, True,
                 [("isr_gnu", 1), ("isr_keil", 2), ("isr_internal", 3),
                  ("isr_early", 4), ("isr_array", 5), ("isr_spill", 6),
                  ("isr_xtu", 8), ("isr_multi", 9)]),
}

HELPER_SYMBOLS = ["helper_ordinary", "helper_multi"]

CONTRACT = "1,1,32,8,1"  # the clang chain contract (V1 layout, AS0 code)

# Frozen A4/A5 constants for the independent image check.
VEC_BASE = 0xFF0003
VEC_STRIDE = 8
VEC_COUNT = 52
NON_LEGAL = frozenset({7, 13, 14, 15, 22, 23, 31, 32, 33, 34, 35, 45, 46})
SYSTEM = frozenset({14, 15})
DEFAULT_WORD = bytes.fromhex("C2AF80FE")
BOOT_FLOOR = 0xFF0210
FLASH_BASE = 0xFF0000
FLASH_SIZE = 0x10000
LINK_AREAS = ["--area-start=HOME=0xff0000", "--area-start=BOOT=0xff0210",
              "--area-start=CSEG=0xff0400", "--area-start=XINIT=0xff8000",
              "--area-start=DSEG=0x30"]

# Link layout rationale (recorded in the JSON notes): DSEG starts at 0x30 so
# no C global can alias the register-file window [0x00,0x20) or the CRT-owned
# BSEG_BYTES window [0x20,0x30). The CRT clears the bit-byte window and no C
# construct can place data there in this campaign.

NEGATIVES = [
    ("neg-slot7", "interrupt(7) reserved slot",
     "MCS251 interrupt vector must be a legal slot in 0-51", """
void bad(void) __attribute__((interrupt(7)));
"""),
    ("neg-slot52", "interrupt(52) out of profile",
     "MCS251 interrupt vector must be a legal slot in 0-51", """
void bad(void) __attribute__((interrupt(52)));
"""),
    ("neg-late", "attribute after a plain first declaration",
     "identity must be established on the first declaration", """
void bad(void);
void bad(void) __attribute__((interrupt(4)));
"""),
    ("neg-call", "ordinary call of an ISR",
     "cannot be used as an ordinary function", """
void bad(void) __attribute__((interrupt(1)));
void caller(void) { bad(); }
"""),
    ("neg-addr", "ISR address taken as an ordinary value",
     "cannot be used as an ordinary function", """
void bad(void) __attribute__((interrupt(1)));
void sink(void);
void caller(void) { void (*p)(void) = bad; sink(); }
"""),
    ("neg-dup", "duplicate slot in one TU",
     "duplicate MCS251 interrupt vector", """
void a(void) __attribute__((interrupt(8)));
void a(void) {}
void b(void) __attribute__((interrupt(8)));
void b(void) {}
"""),
    ("neg-selfcall", "ISR calling itself",
     "cannot be used as an ordinary function", """
void bad(void) __attribute__((interrupt(1)));
void bad(void) { bad(); }
"""),
]

NEG_KEIL_NOFLAG = ("neg-keil-noflag",
                   "Keil suffix without -fmcs251-keil must not compile", None,
                   "void bad() interrupt 1 {}\n")


def die(msg):
    sys.exit("compile-matrix: FAIL: " + msg)


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def tool_identity(path):
    if not os.path.isabs(path):
        die("tool path must be absolute: %s" % path)
    if not os.path.exists(path):
        die("tool missing: %s" % path)
    rc, out = run([path, "--version"])
    first = out.splitlines()[0] if out.splitlines() else ""
    return {"path": path, "version_first_line": first, "sha256":
            sha256_file(path)}


# --------------------------------------------------------------------------
# Minimal ELF32 MSB reader (objects are ET_REL; no objcopy in the frozen
# interface, so section bytes come from this parser and every structural fact
# is cross-checked against llvm-readobj output where used).
# --------------------------------------------------------------------------

class Obj:
    def __init__(self, path):
        self.path = path
        b = open(path, "rb").read()
        self.data = b
        if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2:
            die("%s: not an ELF32/MSB object" % path)
        self.e_type, self.e_machine = struct.unpack_from(">HH", b, 16)
        self.shoff, = struct.unpack_from(">I", b, 32)
        self.shentsize, self.shnum = struct.unpack_from(">HH", b, 46)
        self.shstrndx, = struct.unpack_from(">H", b, 50)
        self.sections = []
        for i in range(self.shnum):
            off = self.shoff + i * self.shentsize
            (nameoff, shtype, flags, addr, offset, size, link, info,
             align, entsize) = struct.unpack_from(">IIIIIIIIII", b, off)
            self.sections.append({
                "nameoff": nameoff, "type": shtype, "flags": flags,
                "addr": addr, "offset": offset, "size": size, "link": link,
                "info": info, "align": align, "entsize": entsize,
                "name": "", "_off": off})
        shstr = self.sections[self.shstrndx]
        strtab = b[shstr["offset"]:shstr["offset"] + shstr["size"]]
        for s in self.sections:
            end = strtab.index(b"\x00", s["nameoff"])
            s["name"] = strtab[s["nameoff"]:end].decode()
        self.by_name = {}
        for s in self.sections:
            self.by_name.setdefault(s["name"], s)
        self.symbols = []
        symtab = None
        for s in self.sections:
            if s["type"] == 2:  # SHT_SYMTAB
                symtab = s
        if symtab:
            strsec = self.sections[symtab["link"]]
            strings = b[strsec["offset"]:strsec["offset"] + strsec["size"]]
            n = symtab["size"] // symtab["entsize"]
            for i in range(n):
                off = symtab["offset"] + i * symtab["entsize"]
                nameoff, value, size, info, other, shndx = struct.unpack_from(
                    ">IIIBBH", b, off)
                end = strings.index(b"\x00", nameoff)
                self.symbols.append({
                    "index": i, "name": strings[nameoff:end].decode(),
                    "value": value, "size": size, "info": info,
                    "type": info & 0xF, "bind": info >> 4, "shndx": shndx})

    def section_bytes(self, name):
        s = self.by_name.get(name)
        if not s:
            return None
        return self.data[s["offset"]:s["offset"] + s["size"]]

    def rela(self, name):
        """Parsed RELA entries [(offset, type, symindex, addend)]."""
        s = self.by_name.get(name)
        if not s:
            return None
        out = []
        for i in range(s["size"] // 12):
            off = s["offset"] + i * 12
            r_off, r_info, r_add = struct.unpack_from(">IIi", self.data, off)
            out.append((r_off, r_info & 0xFF, r_info >> 8, r_add))
        return out


# --------------------------------------------------------------------------
# IR-level checks
# --------------------------------------------------------------------------

def isr_define_present(ir_text, name):
    # define [internal|dso_local|linkonce...] mcs251_intrcc void @name()
    return re.search(r"define\s+[\w ]*mcs251_intrcc\s+void\s+@%s\s*\(\s*\)"
                     % re.escape(name), ir_text) is not None


def slot_attr_present(ir_text, slot):
    return '"mcs251-isr-vector"="%d"' % slot in ir_text


def used_root_covers(ir_text, names):
    m = re.search(r"@llvm\.used = appending global[^\n]*", ir_text)
    if not m:
        return False, "@llvm.used not found"
    blob = m.group(0)
    while "]" not in blob:
        nxt = ir_text[m.end():]
        # Continuation lines (defensive; the IR writer prints one line).
        rest = ir_text[m.end():ir_text.find("\n", m.end()) + 1]
        if not rest:
            break
        blob += rest
    missing = [n for n in names if ("@%s" % n) not in blob]
    if missing:
        return False, "llvm.used root missing: %s" % ",".join(missing)
    return True, ""


def check_ir(ir_text, isr_syms, helper_expect):
    """Returns (ok, problems, facts). helper_expect: IR may reference helpers
    (firmware) or not (helper TU)."""
    problems = []
    facts = {}
    for name, slot in isr_syms:
        if not isr_define_present(ir_text, name):
            problems.append("no mcs251_intrcc definition for @%s" % name)
        if not slot_attr_present(ir_text, slot):
            problems.append("no slot attribute for slot %d" % slot)
    ok, msg = used_root_covers(ir_text, [n for n, _ in isr_syms])
    if isr_syms and not ok:
        problems.append(msg)
    # Helpers: no interrupt convention, no slot attribute (card step 6).
    for line in ir_text.splitlines():
        for h in HELPER_SYMBOLS:
            if re.search(r"define\s+[\w ]*mcs251_intrcc\s+void\s+@%s\s*\("
                         % h, line):
                problems.append("helper @%s carries CC128" % h)
    # Any attribute string naming an ISR slot must belong to an ISR function:
    # count slot attribute strings, they must equal the number of ISRs, and
    # attribute groups referenced by helper defines must not contain the
    # attribute.
    slot_attrs = re.findall(r'"mcs251-isr-vector"="(\d+)"', ir_text)
    facts["slot_attr_values"] = sorted(int(x) for x in slot_attrs)
    if helper_expect is not None:
        expected = sorted(slot for _, slot in helper_expect)
        if facts["slot_attr_values"] != expected:
            problems.append("slot attribute set %s != expected ISR set %s"
                            % (facts["slot_attr_values"], expected))
        attrs = {}
        for m in re.finditer(r"attributes\s+#(\d+)\s*=\s*\{([^}]*)\}",
                             ir_text):
            attrs["#" + m.group(1)] = m.group(2)
        for line in ir_text.splitlines():
            for h in HELPER_SYMBOLS:
                if re.search(r"define[^(]*@%s\s*\(" % h, line):
                    for ref in re.findall(r"#\d+", line):
                        group = attrs.get(ref, "")
                        if "mcs251-isr-vector" in group:
                            problems.append(
                                "helper @%s references slot attribute via %s"
                                % (h, ref))
    return (not problems), problems, facts


# --------------------------------------------------------------------------
# Object-level checks
# --------------------------------------------------------------------------

ISR_RECORD = ">HHBBBBHHIII"  # 24B, big endian (A3.2)
RELOC_TYPE_ISR_REF = 9


def check_object(obj_path, isr_syms, symbol_prefix="_"):
    """Card step 4: exactly paired records 24B x 2N; type9 associations."""
    problems = []
    facts = {"records": []}
    o = Obj(obj_path)
    meta = o.section_bytes(".mcs251.isr")
    if meta is None:
        return False, ["object has no .mcs251.isr section"], facts
    if len(meta) % 24 != 0:
        return False, [".mcs251.isr size %d is not a multiple of 24"
                       % len(meta)], facts
    n_isrs = len(isr_syms)
    if len(meta) != 24 * 2 * n_isrs:
        problems.append(".mcs251.isr size %d != 24*2*%d" % (len(meta), n_isrs))
    sec = o.by_name[".mcs251.isr"]
    if sec["type"] != 1 or sec["flags"] != 0 or sec["align"] != 4:
        problems.append(".mcs251.isr header (type/flags/align) mismatch")
    for r in range(len(meta) // 24):
        fields = struct.unpack_from(ISR_RECORD, meta, r * 24)
        (ver, recsize, kind, entry, hw, save, slot, caps, symref, asset,
         reserved) = fields
        facts["records"].append(
            {"index": r, "kind": kind, "entry_kind": entry, "hw": hw,
             "save": save, "slot": slot, "asset": asset})
        if ver != 1 or recsize != 24:
            problems.append("record %d: version/recsize %d/%d" %
                            (r, ver, recsize))
        if kind not in (1, 2):
            problems.append("record %d: unexpected kind %d" % (r, kind))
        if entry != 1 or hw != 1 or save != 1:
            problems.append("record %d: entry/hw/save %d/%d/%d" %
                            (r, entry, hw, save))
        if caps != 1 or reserved != 0 or asset != 0:
            problems.append("record %d: caps/asset/reserved %d/%d/%d" %
                            (r, caps, asset, reserved))
        if symref != 0:
            problems.append("record %d: symbol_reference bytes nonzero" % r)
        if slot not in [s for _, s in isr_syms]:
            problems.append("record %d: unexpected slot %d" % (r, slot))
    # Pairing: per slot exactly one kind1 followed by one kind2, adjacent.
    seen = {}
    recs = facts["records"]
    for i in range(0, len(recs) - 1, 2):
        a, b = recs[i], recs[i + 1]
        if a["kind"] != 1 or b["kind"] != 2 or a["slot"] != b["slot"]:
            problems.append("records %d/%d not an ENTRY/REGISTER pair for one "
                            "slot" % (i, i + 1))
        seen[a["slot"]] = seen.get(a["slot"], 0) + 1
    for _, slot in isr_syms:
        if seen.get(slot, 0) != 1:
            problems.append("slot %d does not have exactly one pair" % slot)
    # RELA: one zero-width type9 per record at offset record+12, naming the
    # exact STT_FUNC symbol of that slot's ISR (A3.4).
    rela = o.rela(".rela.mcs251.isr")
    if rela is None:
        problems.append("missing .rela.mcs251.isr")
        rela = []
    if len(rela) != 2 * n_isrs:
        problems.append("rela count %d != %d" % (len(rela), 2 * n_isrs))
    syms = {s["index"]: s for s in o.symbols}
    for (r_off, r_type, r_sym, r_add) in rela:
        if r_type != RELOC_TYPE_ISR_REF:
            problems.append("non-type9 relocation in .rela.mcs251.isr")
            continue
        if r_add != 0:
            problems.append("type9 addend %d at offset %d" % (r_add, r_off))
        rec_index = (r_off - 12) // 24
        if (r_off - 12) % 24 != 0 or not 0 <= rec_index < len(recs):
            problems.append("type9 offset %d does not address a record's "
                            "symbol_reference field" % r_off)
            continue
        slot = recs[rec_index]["slot"]
        want = symbol_prefix + next(n for n, s in isr_syms if s == slot)
        sym = syms.get(r_sym, {})
        if sym.get("name") != want:
            problems.append("type9 at %d names %r, want %r"
                            % (r_off, sym.get("name"), want))
        elif sym.get("type") != 2:
            problems.append("type9 target %s is not STT_FUNC" % want)
    facts["isr_section_size"] = len(meta)
    return (not problems), problems, facts


def object_helper_clean(obj_path):
    s = Obj(obj_path).by_name.get(".mcs251.isr")
    if s is not None and s["size"] > 0:
        return False, "helper object carries .mcs251.isr records"
    return True, ""


def isr_entry_addresses(obj_path, isr_syms, symbol_prefix="_"):
    """Symbol offsets inside the object's code section (for exact target
    verification against the map's section base)."""
    o = Obj(obj_path)
    out = {}
    for name, slot in isr_syms:
        want = symbol_prefix + name
        for s in o.symbols:
            if s["name"] == want and s["type"] == 2:
                out[slot] = s["value"]
    return out


def global_symbol_offsets(obj_path, names, symbol_prefix="_"):
    o = Obj(obj_path)
    out = {}
    for s in o.symbols:
        if s["name"] in names:
            out[s["name"]] = {"offset": s["value"], "size": s["size"]}
    return out


# --------------------------------------------------------------------------
# Final-assembly evidence checks (array frame / spill)
#
# The matrix consumes the same post-clang IR the object step consumes, but
# runs llc with -filetype=asm so the claims behind the T09_CASE_ARRAY and
# T09_CASE_SPILL families are checked against the final machine code itself,
# independently of the metadata checks above. Instruction shapes asserted
# here (llc MCS251 house style):
#
#   inc spx, #0xN           prologue frame allocation (stack grows UP, the
#                           local frame is the inc-spx sum; an ISR's fixed
#                           37B push save is separate and never counted)
#   mov @dr60-0xNNNN, rX /  stack-slot access (MOV8mrS/MOV8rmS after frame-
#   mov rX, @dr60-0xNNNN    index elimination; @dr60 is spx, the frame base)
#   ecall _spill_mix        an ordinary noinline call boundary
#
# Both checks scope themselves to the ISR's own body (label line up to its
# reti), so the "all" case checks the same facts on the same functions in a
# multi-ISR translation unit.
# --------------------------------------------------------------------------

def extract_isr_body(asm_text, func_name):
    """Lines of one ISR body: from its label line to the terminating reti."""
    m = re.search(r"^%s:[^\n]*\n" % re.escape(func_name), asm_text, re.M)
    if not m:
        return None
    rest = asm_text[m.end():]
    end = rest.find("\treti")
    return rest[:end] if end >= 0 else rest


STACK_SLOT_STORE = r"^\tmov @dr60(?:-0x[0-9a-f]+|\+0x[0-9a-f]+)?, "
STACK_SLOT_LOAD = r"^\tmov (?:r[0-9]+|wr[0-9]+), @dr60(?:-0x[0-9a-f]+|\+0x[0-9a-f]+)?"


def check_array_frame_asm(asm_text):
    """isr_array claim: the eight volatile bytes of buf[] occupy the ISR
    stack frame at every level. Evidence: (a) the inc-spx steps allocate at
    least the 8 array bytes, and (b) at least two array elements are
    addressed as stack slots >= 4 bytes below the frame top (buf[0] sits at
    -0x0007..-0x0009 depending on how k is framed, buf[3] at -0x0004..)."""
    body = extract_isr_body(asm_text, "_isr_array")
    if body is None:
        return False, ["_isr_array not found in the final assembly"], {}
    frame_bytes = sum(int(x, 16) for x in re.findall(
        r"^\tinc spx, #0x([0-9a-f]+)", body, re.M))
    deep_accesses = len(re.findall(r"@dr60-0x000[4-9a-f]", body))
    problems = []
    if frame_bytes < 8:
        problems.append(
            "isr_array allocates %d stack bytes, want >= 8: the volatile "
            "buf[8] does not occupy the ISR frame" % frame_bytes)
    if deep_accesses < 2:
        problems.append(
            "isr_array has %d deep stack-slot element accesses "
            "(@dr60-0x0004..-0x000f), want >= 2: the array is not addressed "
            "in the frame" % deep_accesses)
    facts = {"frame_bytes": frame_bytes, "deep_element_accesses": deep_accesses}
    return (not problems), problems, facts


def check_spill_asm(asm_text):
    """isr_spill claim: ten unknown values stay live across ten noinline
    call boundaries (every register except spx is caller-saved, so liveness
    across a boundary forces spill slots). Evidence: exactly 10
    `ecall _spill_mix` boundaries, >= 5 stack-slot stores, >= 5 stack-slot
    loads, and >= 4 distinct frame displacements in use."""
    body = extract_isr_body(asm_text, "_isr_spill")
    if body is None:
        return False, ["_isr_spill not found in the final assembly"], {}
    calls = len(re.findall(r"^\tecall\s+_spill_mix$", body, re.M))
    stores = len(re.findall(STACK_SLOT_STORE, body, re.M))
    loads = len(re.findall(STACK_SLOT_LOAD, body, re.M))
    displacements = set(m.group(0) for m in re.finditer(
        r"@dr60(?:-0x[0-9a-f]+|\+0x[0-9a-f]+)?(?=[,)])", body))
    problems = []
    if calls != 10:
        problems.append(
            "isr_spill issues %d noinline _spill_mix calls, want exactly 10"
            % calls)
    if stores < 5 or loads < 5:
        problems.append(
            "isr_spill has %d stack-slot stores / %d loads, want >= 5 of "
            "each: values are not spilled across the call boundaries"
            % (stores, loads))
    if len(displacements) < 4:
        problems.append(
            "isr_spill uses %d distinct @spx-relative frame displacements, "
            "want >= 4: no real multi-slot spill frame" % len(displacements))
    facts = {"noinline_calls": calls, "stack_slot_stores": stores,
             "stack_slot_loads": loads,
             "distinct_displacements": len(displacements)}
    return (not problems), problems, facts


# case -> final-assembly assertions run on the case's llc -filetype=asm
# output (the "all" TU contains both ISRs, so both claims are asserted).
ASM_EVIDENCE = {
    "array": [("array_frame", check_array_frame_asm)],
    "spill": [("spill_traffic", check_spill_asm)],
    "all":   [("array_frame", check_array_frame_asm),
              ("spill_traffic", check_spill_asm)],
}


# --------------------------------------------------------------------------
# Map + image checks
# --------------------------------------------------------------------------

def parse_map(map_path):
    rows = {}
    sections = []
    synth = {}
    stack_line = None
    for line in open(map_path):
        t = line.split()
        if len(t) >= 4 and t[0] == "IRQ":
            rows[int(t[1])] = (int(t[2], 16), t[3],
                               t[4] if len(t) > 4 else "")
            continue
        if len(t) == 3 and t[1] == "=" and t[2].startswith("0x"):
            synth[t[0]] = int(t[2], 16)
            continue
        if line.startswith("stack "):
            stack_line = line.strip()
            continue
        m = re.match(r"^(.*):(\S+) (0x[0-9a-f]+) \+(0x[0-9a-f]+)$",
                     line.strip())
        if m:
            sections.append({"path": m.group(1), "name": m.group(2),
                             "addr": int(m.group(3), 16),
                             "size": int(m.group(4), 16)})
    return rows, sections, synth, stack_line


def check_image(elf_path, map_path, registered, isr_syms, fw_obj_path):
    """Independent PT_LOAD-level image verification (own frozen A4/A5 copy).
    Returns (ok, problems, details)."""
    problems = []
    data = open(elf_path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        return False, ["image is not ELF32/MSB"], {}
    entry, = struct.unpack_from(">I", data, 24)
    phoff, = struct.unpack_from(">I", data, 28)
    phentsize, phnum = struct.unpack_from(">HH", data, 42)
    mem = {}
    segs = []
    for i in range(phnum):
        p = phoff + i * phentsize
        p_type, p_offset = struct.unpack_from(">II", data, p)
        p_vaddr = struct.unpack_from(">I", data, p + 8)[0]
        p_filesz = struct.unpack_from(">I", data, p + 16)[0]
        if p_type != 1:
            continue
        segs.append((p_vaddr, p_filesz))
        for j in range(p_filesz):
            a = p_vaddr + j
            if a in mem:
                problems.append("overlapping PT_LOAD bytes at 0x%x" % a)
                return False, problems, {}
            mem[a] = data[p_offset + j]

    def covered(lo, hi):
        return any(sa < hi and lo < sa + sz for (sa, sz) in segs)

    rows, sections, synth, stack_line = parse_map(map_path)
    if len(rows) != VEC_COUNT:
        problems.append("map has %d IRQ rows, want %d" %
                        (len(rows), VEC_COUNT))

    # Exact registered handler entries: firmware .text base from the map's
    # section row + the ISR symbol offset from the object's symtab.
    # Finding-2 guard: the row is matched by object path (never the first
    # .text row of the whole multi-object link), and more than one .text
    # row for the firmware object is an ambiguous base, rejected here.
    fw_sections = [s for s in sections
                   if os.path.basename(s["path"]) ==
                   os.path.basename(fw_obj_path) and s["name"] == ".text"]
    text_base = None
    if not fw_sections:
        problems.append("no firmware .text section row in map")
    elif len(fw_sections) > 1:
        problems.append("firmware object has %d .text section rows in map, "
                        "base is ambiguous" % len(fw_sections))
    else:
        text_base = fw_sections[0]["addr"]
    offsets = isr_entry_addresses(fw_obj_path, isr_syms)

    defaults = []
    for slot in range(VEC_COUNT):
        base = VEC_BASE + VEC_STRIDE * slot
        if rows.get(slot, (None, "", ""))[0] != base:
            problems.append("map row %d address violates the formula" % slot)
            continue
        tag = rows[slot][1]
        if slot in NON_LEGAL:
            want = "SYSTEM" if slot in SYSTEM else "RESERVED"
            if tag != want:
                problems.append("slot %d map tag %s, want %s"
                                % (slot, tag, want))
            if any(a in mem for a in range(base, base + VEC_STRIDE)):
                problems.append("non-legal slot %d carries payload" % slot)
            if covered(base, base + VEC_STRIDE):
                problems.append("non-legal slot %d covered by PT_LOAD" % slot)
            continue
        if mem.get(base) != 0x8A:
            problems.append("legal slot %d lacks the EJMP opcode" % slot)
            continue
        target = int.from_bytes(bytes(mem[base + k] for k in (1, 2, 3)),
                                "big")
        if any(a in mem for a in range(base + 4, base + VEC_STRIDE)):
            problems.append("legal slot %d tail carries payload" % slot)
        if covered(base + 4, base + VEC_STRIDE):
            problems.append("legal slot %d tail covered by PT_LOAD" % slot)
        if slot in registered:
            if text_base is None or slot not in offsets:
                problems.append("slot %d: no independent handler entry"
                                % slot)
            else:
                want = text_base + offsets[slot]
                if want not in mem:
                    problems.append("slot %d handler entry 0x%x has no "
                                    "payload" % (slot, want))
                if target != want:
                    problems.append("slot %d EJMP 0x%x != handler entry 0x%x"
                                    % (slot, target, want))
        else:
            defaults.append(target)
    if defaults and len(set(defaults)) != 1:
        problems.append("unregistered legal slots do not share one target")
    if defaults:
        d = defaults[0]
        got = bytes(mem.get(d + i, 0) for i in range(4))
        if got != DEFAULT_WORD:
            problems.append("default entry 0x%x holds %s, want C2AF80FE"
                            % (d, got.hex().upper()))
        for slot in registered:
            base = VEC_BASE + VEC_STRIDE * slot
            t = int.from_bytes(bytes(mem[base + k] for k in (1, 2, 3)),
                               "big") if base in mem else None
            if t == d:
                problems.append("registered slot %d jumps to the default "
                                "entry" % slot)
    # Reset: exactly a 3-byte ljmp at HOME abutting the vector base.
    if entry != FLASH_BASE:
        problems.append("entry 0x%x is not 0x%x" % (entry, FLASH_BASE))
    elif mem.get(entry) != 0x02:
        problems.append("reset does not begin with the ljmp opcode")
    else:
        field = int.from_bytes(bytes(mem[entry + 1], ).hex() and
                               bytes(mem[entry + i] for i in (1, 2)), "big")
        bank = (entry + 3) & 0xFF0000
        boot = bank | field
        if boot < BOOT_FLOOR:
            problems.append("reset J16 target 0x%x below BOOT floor" % boot)
        if boot not in mem:
            problems.append("reset J16 target 0x%x has no payload" % boot)

    # QEMU-PENDING expectations: DSEG globals from the object symtab mapped
    # through the map's DSEG section row.
    expect = []
    fw_dseg = [s for s in sections
               if os.path.basename(s["path"]) ==
               os.path.basename(fw_obj_path) and
               s["name"].startswith(".mcs251.dseg")]
    gsyms = global_symbol_offsets(fw_obj_path,
                                  ["_g_xinit_a", "_g_xinit_b", "_g_zero"])
    if fw_dseg and gsyms:
        daddr = fw_dseg[0]["addr"]
        for sym, base_bytes in (("_g_xinit_a", bytes([0xA5])),
                                ("_g_xinit_b", bytes([0x12, 0x34])),
                                ("_g_zero", bytes([0x00]))):
            g = gsyms.get(sym)
            if not g:
                problems.append("global %s missing from object symtab" % sym)
                continue
            expect.append({
                "symbol": sym, "address": daddr + g["offset"],
                "size": g["size"], "expected_bytes": base_bytes.hex().upper(),
                "source": "XINIT record via CRT walker",
                "status": "QEMU-PENDING"})
    expect.append({
        "symbol": "BSEG_BYTES_window", "address": 0x20, "size": 16,
        "expected_bytes": "00" * 16,
        "source": "CRT clear (no C variable is placed in the bit window "
                  "under DSEG=0x30)",
        "status": "QEMU-PENDING"})
    for slot, off in sorted(offsets.items()):
        if text_base is not None:
            expect.append({
                "symbol": "isr_slot_%d_entry" % slot,
                "address": text_base + off, "size": None,
                "expected_bytes": None,
                "source": "vector EJMP target (for T10 IRQ injection)",
                "status": "QEMU-PENDING"})

    details = {"default_target": (hex(defaults[0]) if defaults else None),
               "isr_entries": {str(s): hex(text_base + o)
                               if text_base is not None and s in offsets
                               else None
                               for s, o in offsets.items()},
               "stack_line": stack_line,
               "qemu_expectations": expect}
    return (not problems), problems, details


# --------------------------------------------------------------------------
# Main flow
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clang", required=True)
    ap.add_argument("--llc", required=True)
    ap.add_argument("--opt", required=True)
    ap.add_argument("--lld", required=True)
    ap.add_argument("--yaml2obj", required=True)
    ap.add_argument("--readobj", required=True)
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args()

    out = args.out_dir
    if not os.path.isabs(out):
        die("--out-dir must be absolute")
    for d in ("ir", "opt-ir", "obj", "asm", "image", "negatives", "crt",
              "logs"):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    tools = {name: tool_identity(path) for name, path in
             (("clang", args.clang), ("llc", args.llc), ("opt", args.opt),
              ("lld", args.lld), ("yaml2obj", args.yaml2obj),
              ("readobj", args.readobj))}

    # CRT object + the frozen T08 checker.
    crt_o = os.path.join(out, "crt", "crt-irq.o")
    rc, log = run([args.yaml2obj, CRT_YAML, "-o", crt_o])
    if rc != 0:
        die("yaml2obj on crt-irq.yaml failed:\n" + log)
    rc_crt, log_crt = run([sys.executable, CHECK_CRT_IRQ, crt_o])
    if rc_crt != 0:
        die("check-crt-irq.py rejected the CRT object:\n" + log_crt)

    report = {
        "schema": "mcs251-t09-matrix/1",
        "card": "T09",
        "tools": tools,
        "frozen_llc_for_t10": tools["llc"],
        "inputs": {
            "firmware.c": sha256_file(FIRMWARE_C),
            "helper.c": sha256_file(HELPER_C),
            "crt-irq.yaml": sha256_file(CRT_YAML),
            "isr-check-image.py": sha256_file(T07_CHECK_IMAGE),
            "check-crt-irq.py": sha256_file(CHECK_CRT_IRQ),
        },
        "crt": {"object": crt_o, "sha256": sha256_file(crt_o),
                "check_crt_irq": {"rc": rc_crt, "output": log_crt.strip()}},
        "contract": CONTRACT,
        "link_areas": LINK_AREAS,
        "notes": [
            "DSEG=0x30 keeps C globals out of the register-file window "
            "[0x00,0x20) and the CRT-owned BSEG_BYTES window [0x20,0x30).",
            "The C chain uses -mcs251-memory-contract=1,1,32,8,1 for clang "
            "and llc: the clang driver emits AS0 ISR definitions with plain "
            "llvm.used roots under this contract (landed T02/T03 tests).",
            "QEMU runs belong to T10; every runtime expectation below is "
            "marked QEMU-PENDING and the linked images are frozen for it.",
            "R4 acceptance basis (T09 review finding 2, Track A): this "
            "script's own image check proves every registered EJMP "
            "against the exact handler entry = firmware object symtab "
            "st_value + the firmware object's own .text row base from the "
            "map. The frozen T07 isr-check-image.py output recorded per "
            "combo is informational only and is NOT an acceptance gate.",
        ],
        "matrix": [],
        "negatives": [],
        "qemu_pending": [],
    }

    failures = []

    # helper.o per level.
    helper_obj = {}
    for level in OPT_LEVELS:
        llp = os.path.join(out, "ir", "helper-%s.ll" % level)
        cmd = [args.clang, "--target=mcs251-unknown-none",
               "-Xclang", "-mcs251-memory-contract=" + CONTRACT,
               "-" + level, "-S",
               "-emit-llvm", HELPER_C, "-o", llp]
        rc, log = run(cmd)
        if rc != 0:
            die("helper.c %s failed:\n%s" % (level, log))
        text = open(llp).read()
        ok, probs, _ = check_ir(text, [], helper_expect=[])
        if not ok:
            die("helper.c %s IR check failed: %s" % (level, probs))
        op = os.path.join(out, "obj", "helper-%s.o" % level)
        rc, log = run([args.llc, "-mtriple=mcs251",
                       "-mcs251-memory-contract=" + CONTRACT, "-filetype=obj",
                       "-mcs251-object-format=elf", llp, "-o", op])
        if rc != 0:
            die("helper.c %s llc failed:\n%s" % (level, log))
        ok, msg = object_helper_clean(op)
        if not ok:
            die("helper.c %s object carries ISR metadata: %s" % (level, msg))
        helper_obj[level] = op

    for case, (slots, keil, use_helper, isr_syms) in CASES.items():
        for level in OPT_LEVELS:
            entry = {"case": case, "level": level, "slots": slots,
                     "keil_flag": keil, "uses_helper": use_helper}
            stem = "%s-%s" % (case, level)
            llp = os.path.join(out, "ir", stem + ".ll")
            cmd = [args.clang, "--target=mcs251-unknown-none",
                   "-Xclang", "-mcs251-memory-contract=" + CONTRACT,
                   "-" + level,
                   "-S", "-emit-llvm", "-DT09_CASE_" + case.upper(),
                   FIRMWARE_C, "-o", llp]
            if keil:
                cmd.insert(2, "-fmcs251-keil")
            entry["command_clang"] = cmd
            rc, log = run(cmd)
            entry["clang_rc"] = rc
            if rc != 0:
                entry["status"] = "FAIL"
                entry["fail_log"] = log[-4000:]
                failures.append(entry)
                report["matrix"].append(entry)
                continue
            text = open(llp).read()
            ok, probs, facts = check_ir(text, isr_syms,
                                        helper_expect=isr_syms)
            entry["ir_checks"] = {"ok": ok, "problems": probs,
                                  "slot_attrs": facts.get(
                                      "slot_attr_values")}
            optp = os.path.join(out, "opt-ir", stem + ".opt.ll")
            ocmd = [args.opt, "-passes=globalopt,ipsccp,globaldce,verify",
                    "-S", llp, "-o", optp]
            entry["command_opt"] = ocmd
            rc, log = run(ocmd)
            entry["opt_rc"] = rc
            opt_ok = rc == 0
            opt_probs = []
            opt_facts = {}
            if opt_ok:
                opt_text = open(optp).read()
                opt_ok, opt_probs, opt_facts = check_ir(
                    opt_text, isr_syms, helper_expect=isr_syms)
            else:
                opt_probs = ["opt failed: " + log[-2000:]]
            entry["opt_retention"] = {"ok": opt_ok, "problems": opt_probs,
                                      "slot_attrs": opt_facts.get(
                                          "slot_attr_values")}
            objp = os.path.join(out, "obj", stem + ".o")
            lcmd = [args.llc, "-mtriple=mcs251",
                    "-mcs251-memory-contract=" + CONTRACT, "-filetype=obj",
                    "-mcs251-object-format=elf", llp, "-o", objp]
            entry["command_llc"] = lcmd
            rc, log = run(lcmd)
            entry["llc_rc"] = rc
            obj_ok = rc == 0
            obj_probs = []
            rec_facts = {}
            if obj_ok:
                obj_ok, obj_probs, rec_facts = check_object(objp, isr_syms)
                hok, hmsg = object_helper_clean(
                    helper_obj[level]) if use_helper else (True, "")
                if not hok:
                    obj_ok = False
                    obj_probs.append(hmsg)
            else:
                obj_probs = ["llc failed: " + log[-2000:]]
            entry["object"] = {"path": objp,
                               "sha256": sha256_file(objp) if obj_ok else None,
                               "ok": obj_ok, "problems": obj_probs,
                               "records": rec_facts.get("records")}
            # Final-assembly evidence for the array-frame / spill coverage
            # claims: same IR input as the object step, llc -filetype=asm,
            # independent text assertions. Missing evidence FAILs the combo.
            asm_ok = True
            asm_probs = []
            asm_facts = {}
            evidence = ASM_EVIDENCE.get(case)
            asmp = None
            if evidence:
                asmp = os.path.join(out, "asm", stem + ".s")
                scmd = [args.llc, "-mtriple=mcs251",
                        "-mcs251-memory-contract=" + CONTRACT,
                        "-filetype=asm", llp, "-o", asmp]
                entry["command_llc_asm"] = scmd
                rc, log = run(scmd)
                entry["llc_asm_rc"] = rc
                if rc != 0:
                    asm_ok = False
                    asm_probs.append("llc -filetype=asm failed: "
                                     + log[-2000:])
                else:
                    asm_text = open(asmp).read()
                    for label, check in evidence:
                        ok, probs, facts = check(asm_text)
                        asm_facts[label] = facts
                        asm_probs.extend("%s: %s" % (label, p)
                                         for p in probs)
                        asm_ok = asm_ok and ok
            entry["asm_evidence"] = {"path": asmp, "ok": asm_ok,
                                     "problems": asm_probs,
                                     "facts": asm_facts}
            elfp = os.path.join(out, "image", stem + ".elf")
            mapp = os.path.join(out, "image", stem + ".map")
            inputs = [crt_o, objp] + ([helper_obj[level]] if use_helper
                                      else [])
            kcmd = ([args.lld] + inputs + LINK_AREAS +
                    ["--flash-base=0x%x" % FLASH_BASE,
                     "--flash-size=0x%x" % FLASH_SIZE, "--map=" + mapp,
                     "-o", elfp])
            entry["command_link"] = kcmd
            rc, log = run(kcmd)
            entry["link_rc"] = rc
            link_ok = rc == 0
            if not link_ok:
                entry["link_problems"] = [log[-2000:]]
            chk_ok = False
            chk_probs = []
            chk_details = {}
            t07 = {"rc": None, "output": None, "role":
                   "informational only (T09 review finding 2, Track A): "
                   "the 41-byte-per-slot expectation is a lit-fixture "
                   "convention real firmware does not satisfy; not an "
                   "acceptance gate"}
            if link_ok:
                rc, log = run([sys.executable, T07_CHECK_IMAGE, elfp, mapp,
                               "--registered", ",".join(str(s) for s in
                                                        slots)])
                t07["rc"], t07["output"] = rc, log.strip()
                chk_ok, chk_probs, chk_details = check_image(
                    elfp, mapp, set(slots), isr_syms, objp)
            entry["t07_isr_check_image"] = t07
            entry["own_image_check"] = {"ok": chk_ok,
                                        "problems": chk_probs,
                                        "default_target":
                                            chk_details.get(
                                                "default_target"),
                                        "isr_entries":
                                            chk_details.get("isr_entries")}
            entry["qemu_pending"] = chk_details.get("qemu_expectations", [])
            if link_ok:
                entry["image"] = {"elf": elfp, "map": mapp,
                                  "sha256": sha256_file(elfp)}
            stage_ok = (entry.get("ir_checks", {}).get("ok") and
                        entry.get("opt_retention", {}).get("ok") and
                        entry.get("object", {}).get("ok") and
                        entry.get("asm_evidence", {}).get("ok") and
                        link_ok and chk_ok)
            entry["status"] = "PASS" if stage_ok else "FAIL"
            if not stage_ok:
                failures.append(entry)
            report["matrix"].append(entry)
            report["qemu_pending"].extend(
                dict(e, case=case, level=level)
                for e in entry["qemu_pending"])
            print("%-9s %-3s %s" % (case, level, entry["status"]),
              flush=True)

    # ---- Negative cases (card step 7): frozen diagnostics, no objects ----
    for name, desc, diag, src in NEGATIVES + [NEG_KEIL_NOFLAG]:
        cpath = os.path.join(out, "negatives", name + ".c")
        with open(cpath, "w") as f:
            f.write(src)
        cmd = [args.clang, "--target=mcs251-unknown-none",
               "-Xclang", "-mcs251-memory-contract=" + CONTRACT, "-O2",
               "-S",
               "-emit-llvm", cpath, "-o", os.path.join(out, "negatives",
                                                       name + ".ll")]
        rc, log = run(cmd)
        got_diag = diag in log if diag else None
        produced = os.path.exists(os.path.join(out, "negatives",
                                               name + ".ll"))
        ok = rc != 0 and not produced and (diag is None or got_diag)
        report["negatives"].append({
            "name": name, "description": desc,
            "expected_diagnostic": diag, "clang_rc": rc,
            "diagnostic_matched": got_diag,
            "output_produced": produced,
            "output": log[-2000:], "status": "PASS" if ok else "FAIL"})
        if not ok:
            failures.append(report["negatives"][-1])
        print("%-18s %s" % (name, report["negatives"][-1]["status"]),
              flush=True)

    report["summary"] = {
        "cases": len(report["matrix"]),
        "cases_pass": sum(1 for e in report["matrix"]
                          if e.get("status") == "PASS"),
        "negatives": len(report["negatives"]),
        "negatives_pass": sum(1 for e in report["negatives"]
                              if e["status"] == "PASS"),
        "r4_basis": "own_check_image",
        "t07_checker_pass_informational": sum(
            1 for e in report["matrix"]
            if e.get("t07_isr_check_image", {}).get("rc") == 0),
        "failures": len(failures),
    }
    jpath = os.path.join(out, "matrix-result.json")
    with open(jpath, "w") as f:
        json.dump(report, f, indent=2)
    print("JSON: %s" % jpath)
    if failures:
        print("compile-matrix: %d FAILURES" % len(failures))
        return 1
    print("compile-matrix: ALL GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
