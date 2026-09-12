#!/usr/bin/env python3
"""BT06 X4 extension: convert the official DEF.H common type/macro header.

Part of the Keil-dialect compatibility package (BT06 precedent,
c3f81cee3).  The official STC32G144K246 demo corpus carries a common
``COMM/DEF.H`` with the BYTE/WORD family of typedefs and register helper
macros; non-USB demos include it (directly or through stc.h/config.h).
This generator converts that header into
``include/mcs251_type_compat.h`` with the same discipline as the sbit
layer:

* the official DEF.H is pinned by sha256 - no partial-coverage mode;
* every name declared in DEF.H is accounted for as exactly ONE of
  ``mapped`` (emitted), ``external`` (owned by a standard header, never
  emitted here) or ``rejected`` (a precise compile-time error), and
  classify() exits on any name outside the frozen disposition table, so
  an unaccounted fourth bucket is structurally impossible;
* regeneration is byte-reproducible; ``--check-header`` verifies the
  in-tree header against regeneration.

Width ruling (recorded per name in the ledger): Keil C251 ``int`` is
16-bit, the MCS-251 LLVM target's ``int`` is 32-bit (ShortWidth=16,
IntWidth=LongWidth=32, MCS251.h).  ``WORD``/``INT`` therefore map to
``unsigned short``/``signed short`` to preserve the official 16-bit
width; ``DWORD``/``LONG`` stay 32-bit.

BOOL ruling: ``typedef bit BOOL`` maps to the ``bit`` dialect type, whose
object code generation is a follow-up slice (BT06 P09, see
mcs251_bit_compat.h approval boundaries).  The name is therefore
REJECTED with a precise error naming the substitute (``_Bool`` /
``unsigned char``), not silently mapped to a widening emulation.

The stdint-shaped names (uint8_t & co.) are owned by C99 <stdint.h>:
using them is legal after ``#include <stdint.h>``, but this header never
redefines them (a redefinition could diverge from the compiler's own
freestanding <stdint.h>).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

DEF_H = Path(
    "/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/COMM/DEF.H")
DEF_H_SHA256 = \
    "a8c2bca717631eb0a95957c96d073a1f3cbe58a5073b90e9fb290ae235a30135"

ROOT = Path(__file__).resolve().parents[1]
HEADER_PATH = ROOT / "include" / "mcs251_type_compat.h"
LEDGER_PATH = ROOT / "type-compat.json"

TYPEDEF_RE = re.compile(
    r"^\s*typedef\s+(?P<type>.+?)\s+(?P<name>[A-Za-z_]\w*)\s*;\s*$")
DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+(?P<name>[A-Za-z_]\w*)"
    r"(?P<params>\([^\)]*\))?\s+(?P<body>.+?)\s*$")

# ---------------------------------------------------------------------------
# Frozen disposition table (the only authority; every DEF.H name must appear
# here exactly once or the generator exits).  Keil source type -> our type.
# ---------------------------------------------------------------------------

REJECT_MSG_BOOL = (
    "MCS251: BOOL maps to the Keil 'bit' type; bit-object code generation "
    "is a follow-up slice (BT06 P09) -- use _Bool or unsigned char and "
    "revisit after bit objects land")

DISPOSITIONS = {
    # --- typedefs -----------------------------------------------------------
    # name: (kind, disposition, payload, reason)
    "BOOL": ("typedef", "rejected", None, REJECT_MSG_BOOL),
    "BYTE": ("typedef", "mapped", "unsigned char", "official unsigned char"),
    "WORD": ("typedef", "mapped", "unsigned short",
             "Keil C251 int is 16-bit, this target's int is 32-bit; "
             "unsigned short preserves the 16-bit width"),
    "DWORD": ("typedef", "mapped", "unsigned long", "32-bit in both worlds"),
    "CHAR": ("typedef", "mapped", "signed char",
             "official signed char; plain char's signedness is "
             "implementation-defined, the official type is not"),
    "INT": ("typedef", "mapped", "signed short",
            "Keil C251 int is 16-bit, this target's int is 32-bit; "
            "signed short preserves the 16-bit width"),
    "LONG": ("typedef", "mapped", "signed long", "32-bit in both worlds"),
    "uint8_t": ("typedef", "external", None,
                "C99 <stdint.h> owns this name; include <stdint.h>"),
    "uint16_t": ("typedef", "external", None,
                 "C99 <stdint.h> owns this name; include <stdint.h>"),
    "uint32_t": ("typedef", "external", None,
                 "C99 <stdint.h> owns this name; include <stdint.h>"),
    "int8_t": ("typedef", "external", None,
               "C99 <stdint.h> owns this name; include <stdint.h>"),
    "int16_t": ("typedef", "external", None,
                "C99 <stdint.h> owns this name; include <stdint.h>"),
    "int32_t": ("typedef", "external", None,
                "C99 <stdint.h> owns this name; include <stdint.h>"),
    "uint8": ("typedef", "mapped", "unsigned char", "official exact"),
    "uint16": ("typedef", "mapped", "unsigned short",
               "16-bit width preserved (see WORD)"),
    "uint32": ("typedef", "mapped", "unsigned long", "official exact"),
    "int8": ("typedef", "mapped", "signed char", "official exact"),
    "int16": ("typedef", "mapped", "signed short",
              "16-bit width preserved (see INT)"),
    "int32": ("typedef", "mapped", "signed long", "official exact"),
    "u8": ("typedef", "mapped", "unsigned char", "official exact"),
    "u16": ("typedef", "mapped", "unsigned short",
            "16-bit width preserved (see WORD)"),
    "u32": ("typedef", "mapped", "unsigned long", "official exact"),
    "s8": ("typedef", "mapped", "signed char", "official exact"),
    "s16": ("typedef", "mapped", "signed short",
            "16-bit width preserved (see INT)"),
    "s32": ("typedef", "mapped", "signed long", "official exact"),
    # --- macros (DEF.H guards these with #ifndef; same shape here) ---------
    "NULL": ("macro", "mapped", "0", "official exact"),
    "LOW": ("macro", "mapped", "0", "official exact"),
    "HIGH": ("macro", "mapped", "1", "official exact"),
    "FALSE": ("macro", "mapped", "0", "official exact"),
    "TRUE": ("macro", "mapped", "1", "official exact"),
    "DISABLE": ("macro", "mapped", "0", "official exact"),
    "ENABLE": ("macro", "mapped", "1", "official exact"),
    "min": ("macro", "mapped", "((a) < (b) ? (a) : (b))",
            "official exact (parameter names a/b)"),
    "max": ("macro", "mapped", "((a) > (b) ? (a) : (b))",
            "official exact (parameter names a/b)"),
    "LOBYTE": ("macro", "mapped", "((BYTE)(n))", "official exact"),
    "HIBYTE": ("macro", "mapped", "((BYTE)(((WORD)(n) >> 8) & 0xff))",
               "official exact"),
    "LOWORD": ("macro", "mapped", "((WORD)(n))", "official exact"),
    "HIWORD": ("macro", "mapped", "((WORD)(((DWORD)(n) >> 16) & 0xffff))",
               "official exact"),
    "MAKEWORD": ("macro", "mapped",
                 "((WORD)(((BYTE)(l)) | ((WORD)((BYTE)(h))) << 8))",
                 "official exact"),
    "MAKELONG": ("macro", "mapped",
                 "((DWORD)(((WORD)(l)) | ((DWORD)((WORD)(h))) << 16))",
                 "official exact"),
    "BYTE0": ("macro", "mapped", "LOBYTE(n)", "official exact"),
    "BYTE1": ("macro", "mapped", "HIBYTE(n)", "official exact"),
    "BYTE2": ("macro", "mapped", "LOBYTE(HIWORD(n))", "official exact"),
    "BYTE3": ("macro", "mapped", "HIBYTE(HIWORD(n))", "official exact"),
    "WORD0": ("macro", "mapped", "LOWORD(n)", "official exact"),
    "WORD2": ("macro", "mapped", "HIWORD(n)", "official exact"),
    # BIT*/PIN* are unguarded in DEF.H; #ifndef guards are added here (the
    # header is opt-in and must never break an including translation unit).
    "BIT0": ("macro", "mapped", "0x01", "official value; guard added"),
    "BIT1": ("macro", "mapped", "0x02", "official value; guard added"),
    "BIT2": ("macro", "mapped", "0x04", "official value; guard added"),
    "BIT3": ("macro", "mapped", "0x08", "official value; guard added"),
    "BIT4": ("macro", "mapped", "0x10", "official value; guard added"),
    "BIT5": ("macro", "mapped", "0x20", "official value; guard added"),
    "BIT6": ("macro", "mapped", "0x40", "official value; guard added"),
    "BIT7": ("macro", "mapped", "0x80", "official value; guard added"),
    "BIT": ("macro", "mapped", "(BIT##b)", "official exact; guard added"),
    "BIT_LN": ("macro", "mapped", "0x0f", "official value; guard added"),
    "BIT_HN": ("macro", "mapped", "0xf0", "official value; guard added"),
    "BIT_ALL": ("macro", "mapped", "0xff", "official value; guard added"),
    "PIN_0": ("macro", "mapped", "BIT0", "official exact; guard added"),
    "PIN_1": ("macro", "mapped", "BIT1", "official exact; guard added"),
    "PIN_2": ("macro", "mapped", "BIT2", "official exact; guard added"),
    "PIN_3": ("macro", "mapped", "BIT3", "official exact; guard added"),
    "PIN_4": ("macro", "mapped", "BIT4", "official exact; guard added"),
    "PIN_5": ("macro", "mapped", "BIT5", "official exact; guard added"),
    "PIN_6": ("macro", "mapped", "BIT6", "official exact; guard added"),
    "PIN_7": ("macro", "mapped", "BIT7", "official exact; guard added"),
    "PIN_ALL": ("macro", "mapped", "BIT_ALL", "official exact; guard added"),
    "CLR_REG_BIT": ("macro", "mapped", "((r) &= ~(b))",
                    "official exact; guard added"),
    "SET_REG_BIT": ("macro", "mapped", "((r) |= (b))",
                    "official exact; guard added"),
    "CPL_REG_BIT": ("macro", "mapped", "((r) ^= (b))",
                    "official exact; guard added"),
    "READ_REG_BIT": ("macro", "mapped", "((r) & (b))",
                     "official exact; guard added"),
    "READ_REG": ("macro", "mapped", "(r)", "official exact; guard added"),
    "WRITE_REG": ("macro", "mapped", "((r) = (v))",
                  "official exact; guard added"),
    "CLR_REG": ("macro", "mapped", "((r) = 0)", "official exact; guard added"),
    "MODIFY_REG": ("macro", "mapped", "((r) = (((r) & ~(clr)) | ((set) & (clr))))",
                   "official exact; guard added"),
}

# min/max keep their official parameter lists; every other mapped macro is
# object-like.
FUNCTION_LIKE_PARAMS = {"min": "(a, b)", "max": "(a, b)",
                        "BIT": "(b)",
                        "LOBYTE": "(n)", "HIBYTE": "(n)", "LOWORD": "(n)",
                        "HIWORD": "(n)", "MAKEWORD": "(l, h)",
                        "MAKELONG": "(l, h)", "BYTE0": "(n)", "BYTE1": "(n)",
                        "BYTE2": "(n)", "BYTE3": "(n)", "WORD0": "(n)",
                        "WORD2": "(n)",
                        "CLR_REG_BIT": "(r, b)", "SET_REG_BIT": "(r, b)",
                        "CPL_REG_BIT": "(r, b)", "READ_REG_BIT": "(r, b)",
                        "READ_REG": "(r)", "WRITE_REG": "(r, v)",
                        "CLR_REG": "(r)", "MODIFY_REG": "(r, clr, set)"}


def parse_def_h(path: Path):
    """Lexically parse DEF.H declarations with physical line numbers.

    Returns [(line, kind, name, official_text)] for every typedef and every
    #define (guarded or not).  Preprocessor conditionals are not evaluated;
    the classifier only needs the declared names, and DEF.H has no
    conditional compilation beyond the #ifndef guards.
    """
    entries = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8",
                                                errors="replace").splitlines(),
                                 start=1):
        m = TYPEDEF_RE.match(raw)
        if m:
            entries.append((lineno, "typedef", m.group("name"),
                            m.group("type")))
            continue
        m = DEFINE_RE.match(raw)
        if m and m.group("name") not in ("__DEF_H__",):
            official = ((m.group("params") or "") + " " + m.group("body"))
            entries.append((lineno, "macro", m.group("name"),
                            official.strip()))
    return entries


def classify(entries):
    """Map every parsed DEF.H name to its frozen disposition.

    Exits loudly on any name missing from DISPOSITIONS (no fourth bucket)
    and on any disposition entry with no matching DEF.H declaration (the
    table must be exactly the official header's name set).
    """
    ledger = []
    seen = set()
    for lineno, kind, name, official in entries:
        if name not in DISPOSITIONS:
            sys.exit("gen-type-compat-header.py: DEF.H line %d declares %r "
                     "which has no frozen disposition (unaccounted name)"
                     % (lineno, name))
        dkind, disposition, payload, reason = DISPOSITIONS[name]
        if dkind != kind:
            sys.exit("gen-type-compat-header.py: DEF.H line %d declares %r "
                     "as %s but the frozen table says %s"
                     % (lineno, name, kind, dkind))
        if name in seen:
            sys.exit("gen-type-compat-header.py: duplicate DEF.H "
                     "declaration of %r at line %d" % (name, lineno))
        seen.add(name)
        # Function-like macros: the official parameter list must match the
        # frozen one exactly (structural fidelity check; body whitespace is
        # normalized by the test's --check-header comparisons instead).
        if kind == "macro":
            if official.startswith("("):
                official_params = official[:official.index(")") + 1]
            else:
                official_params = ""
            if bool(official_params) != (name in FUNCTION_LIKE_PARAMS):
                sys.exit("gen-type-compat-header.py: DEF.H line %d: %r "
                         "function-likeness disagrees with the frozen "
                         "table" % (lineno, name))
            if name in FUNCTION_LIKE_PARAMS:
                norm = lambda s: re.sub(r"\s+", "", s)
                if norm(official_params) != norm(FUNCTION_LIKE_PARAMS[name]):
                    sys.exit("gen-type-compat-header.py: DEF.H line %d: %r "
                             "parameters %s differ from the frozen %s"
                             % (lineno, name, official_params,
                                FUNCTION_LIKE_PARAMS[name]))
        ledger.append({
            "name": name, "kind": kind, "line": lineno,
            "official": official, "disposition": disposition,
            "expansion": payload, "reason": reason,
        })
    missing = set(DISPOSITIONS) - seen
    if missing:
        sys.exit("gen-type-compat-header.py: frozen disposition entries "
                 "with no DEF.H declaration: %s" % sorted(missing))
    return ledger


HEADER_TOP = """\
/*===---- mcs251_type_compat.h - MCS-251 Keil DEF.H type compat ----------===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*/

//===----------------------------------------------------------------------===//
// BT06 X4 extension: thin type/macro compatibility layer for the official
// STC32G144K246 demo corpus common header COMM/DEF.H (the BYTE/WORD family
// and the register helper macros used by the non-USB demos).
//
// GENERATED - DO NOT EDIT BY HAND.  Authoritative source: the sha256-pinned
// official DEF.H plus the frozen disposition table inside
//   validation/mcs251-dialect/tools/gen-type-compat-header.py
// which also writes the full ledger type-compat.json.  Regenerate with:
//   tools/gen-type-compat-header.py
// In-tree consistency is checked by:
//   tools/gen-type-compat-header.py --check-header include/mcs251_type_compat.h
//
// Scope (XDATA-CODE-SLICE-TASK.md X4: "BT06 compat header extension, only
// what the non-USB demos need").  Every DEF.H name is exactly one of:
//   mapped    - emitted below (widths preserved: WORD/INT map to short,
//               Keil C251 int is 16-bit, this target's int is 32-bit);
//   external  - owned by a standard header (the stdint-shaped names);
//               never emitted here, include <stdint.h> for them;
//   rejected  - a precise compile-time error (BOOL: the Keil 'bit' type,
//               whose object code generation is a BT06 P09 follow-up).
//
// This header does NOT define the bare `bit`/`sbit` keywords (frontend,
// -fmcs251-keil), does NOT emit SFR names (use the sfr-convert generated
// stc32g144k246-as6.h) and does NOT emit sbit names (use
// mcs251_bit_compat.h).  Macro definitions carry #ifndef guards: the layer
// is opt-in and must never break an including translation unit.
//===----------------------------------------------------------------------===*/

#ifndef __MCS251_TYPE_COMPAT_H__
#define __MCS251_TYPE_COMPAT_H__
"""

HEADER_BOTTOM = """\
#endif /* __MCS251_TYPE_COMPAT_H__ */
"""


def render(ledger):
    lines = [HEADER_TOP]
    mapped_types = [e for e in ledger if e["kind"] == "typedef"
                    and e["disposition"] == "mapped"]
    rejected = [e for e in ledger if e["disposition"] == "rejected"]
    externals = [e for e in ledger if e["disposition"] == "external"]
    macros = [e for e in ledger if e["kind"] == "macro"
              and e["disposition"] == "mapped"]

    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("// Mapped types (%d): widths preserved against Keil C251."
                 % len(mapped_types))
    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("")
    for e in mapped_types:
        lines.append("typedef %s %s;  // DEF.H line %d: %s"
                     % (e["expansion"], e["name"], e["line"], e["reason"]))
    lines.append("")

    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("// Rejected (%d): precise errors, never silent emulations."
                 % len(rejected))
    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("")
    for e in rejected:
        lines.append('#define %s \\' % e["name"])
        lines.append('    _Pragma("GCC error \\"%s\\"") %s_rejected_marker'
                     % (e["reason"].replace('"', "'"), e["name"]))
    lines.append("")

    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("// External ownership (%d): C99 <stdint.h> provides these;"
                 % len(externals))
    lines.append("// this header never redefines them.")
    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("//")
    for e in externals:
        lines.append("// %s: %s (DEF.H line %d)"
                     % (e["name"], e["reason"], e["line"]))
    lines.append("")

    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("// Mapped macros (%d): official bodies, #ifndef guards"
                 % len(macros))
    lines.append("// added (the layer is opt-in).")
    lines.append("//===------------------------------------------------------" +
                 "----------------===//")
    lines.append("")
    for e in macros:
        params = FUNCTION_LIKE_PARAMS.get(e["name"])
        if params:
            lines.append("#ifndef %s" % e["name"])
            lines.append("#define %s%s  %s" % (e["name"], params,
                                               e["expansion"]))
            lines.append("#endif")
        else:
            lines.append("#ifndef %s" % e["name"])
            lines.append("#define %s  %s" % (e["name"], e["expansion"]))
            lines.append("#endif")
    lines.append("")
    lines.append(HEADER_BOTTOM.rstrip("\n"))
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--def-h", type=Path, default=DEF_H,
                    help="official DEF.H to convert (default: the pinned "
                         "corpus copy)")
    ap.add_argument("--out", type=Path, default=HEADER_PATH,
                    help="output header path")
    ap.add_argument("--ledger", type=Path, default=LEDGER_PATH,
                    help="output ledger JSON path")
    ap.add_argument("--check-header", type=Path, metavar="PATH",
                    help="verify that PATH is byte-identical to "
                         "regeneration instead of writing")
    args = ap.parse_args()

    data = args.def_h.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != DEF_H_SHA256:
        sys.exit("gen-type-compat-header.py: DEF.H sha256 mismatch: %s "
                 "(expected %s) - the official header changed; re-derive "
                 "the disposition table deliberately, never silently"
                 % (digest, DEF_H_SHA256))

    ledger = classify(parse_def_h(args.def_h))
    text = render(ledger)

    if args.check_header is not None:
        current = args.check_header.read_bytes()
        if current != text.encode("utf-8"):
            sys.exit("gen-type-compat-header.py: %s is not byte-identical "
                     "to regeneration - regenerate or fix by hand "
                     "deliberately" % args.check_header)
        print("check-header: OK (%d accounted names: %d mapped typedefs, "
              "%d mapped macros, %d external, %d rejected)"
              % (len(ledger),
                 sum(1 for e in ledger if e["kind"] == "typedef"
                     and e["disposition"] == "mapped"),
                 sum(1 for e in ledger if e["kind"] == "macro"
                     and e["disposition"] == "mapped"),
                 sum(1 for e in ledger if e["disposition"] == "external"),
                 sum(1 for e in ledger if e["disposition"] == "rejected")))
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    args.ledger.write_text(
        json.dumps({"source": str(args.def_h), "sha256": digest,
                    "entries": ledger}, indent=2, ensure_ascii=False)
        + "\n", encoding="utf-8")
    print("generated: %s (%d accounted names) and ledger %s"
          % (args.out, len(ledger), args.ledger))
    return 0


if __name__ == "__main__":
    sys.exit(main())
