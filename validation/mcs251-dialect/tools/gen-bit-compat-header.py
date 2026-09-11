#!/usr/bin/env python3
"""Emit ``mcs251_bit_compat.h`` (BT06) from ``bit-registers.json``.

Why this exists
---------------
BT06 requires a *thin* compatibility header that exposes the auditable first
safe bit-name set through the target builtin
``__builtin_mcs251_bit_lvalue(ICE)``.  The authoritative data lives in
``bit-registers.json`` (produced by ``gen-bit-registers.py`` from the porting
SFR/sbit reports).  This tool renders that JSON into a header and is the only
supported way to (re)generate the header, so the header can never silently
drift from the data.

Contract with ``gen-bit-registers.py --check-header``
-----------------------------------------------------
The header must satisfy ``check_header()``:

* every ``approved_pending_pm`` record appears as
  ``#define NAME __builtin_mcs251_bit_lvalue(0xNN)`` with the JSON bit address;
* every ``restricted`` record appears as ``#define NAME MCS251_error_xxx``
  where ``xxx`` is a single ``[A-Za-z0-9_]+`` token (a function-style macro is
  not accepted by the checker's regex);
* every ``rejected_non_bit_addressable`` record likewise appears as a reject
  macro, preceded by a comment carrying the base SFR and the reason (BT06-1:
  every official sbit is explicitly accounted for, never silently dropped);
* no other name is mapped to the builtin;
* rejected names are never builtin-mapped.

Policy (per BIT-DECISION-20260911 P04/P09 and BIT-TASK-BREAKDOWN.md BT06)
-----------------------------------------------------------------------
* compiler-managed register bits (PSW/ACC/B/shadow/N) -> hard error;
* RSTCFG-related bits (`no bit-addressable base`)        -> hard error;
* XFR-enable control bits (EAXFR)                        -> hard error;
* non-bit-addressable bases (no hardware bit address,
  byte-mask emulation forbidden by P03/P09)              -> hard error;
* every remaining official sbit is converted (BT06-1 reclassification:
  the former not-in-first-slice bucket was struck down as a rejection
  reason for peripheral control bits);
* no ``bit``/``sfr``/``sbit`` keyword is defined here and no bit is emulated
  as u8/_Bool or a byte mask.

``MCS251_RAM_BIT(b)`` range contract (BT06-2)
---------------------------------------------
The macro claims ``b`` is an internal-RAM bit address in 0..127; the claim is
enforced at compile time.  ``_Static_assert`` cannot appear inside the lvalue
expression, so the guard uses the expression-context equivalent: the array
type ``char[(b) >= 0 && (b) <= 127 ? 1 : -1]`` is ill-formed (negative array
size) for any out-of-range constant, and multiplying ``sizeof`` of it by 0
keeps the whole expression an integer constant expression.  Without this
guard, 0x80..0xFF would silently fall through to the SFR bit half of the
builtin's address space (e.g. ``MCS251_RAM_BIT(0xAF)`` would hit EA's
address), which is exactly what the contract forbids.

Determinism
-----------
Output depends only on the JSON, so two runs produce byte-identical headers.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# Human-readable diagnostic per rejection class.
REJECT_MESSAGES = {
    "compiler_managed": (
        "MCS251: compiler-managed register bit is not available through the "
        "ordinary interface"
    ),
    "xfr_control": (
        "MCS251: XFR-enable control bit is not available; enable XFR "
        "explicitly, do not access it as an ordinary bit"
    ),
    "rstcfg_restricted": (
        "MCS251: RSTCFG-related bit is forbidden (no bit-addressable base)"
    ),
    "non_bit_addressable": (
        "MCS251: base SFR is not bit-addressable, no hardware bit address "
        "exists and no byte-mask emulation is provided"
    ),
}


def reject_class(reason: str) -> str:
    """Map a record reason string to a rejection class token."""
    if "XFR-enable" in reason:
        return "xfr_control"
    if "RSTCFG" in reason:
        return "rstcfg_restricted"
    if "non_bit_addressable_base" in reason:
        return "non_bit_addressable"
    return "compiler_managed"


def render(document: dict) -> str:
    records = document["records"]
    approved = sorted(
        (r for r in records if r["status"] == "approved_pending_pm"),
        key=lambda r: r["name"],
    )
    restricted = sorted(
        (r for r in records if r["status"] == "restricted"),
        key=lambda r: r["name"],
    )
    non_bit_addr = sorted(
        (r for r in records if r["status"] == "rejected_non_bit_addressable"),
        key=lambda r: r["name"],
    )
    classes = sorted({reject_class(r["reason"]) for r in restricted + non_bit_addr})

    L: list[str] = []
    L.append("/*===---- mcs251_bit_compat.h - MCS-251 Keil bit-name compatibility ----===*/")
    L.append("/*")
    L.append(" * Part of the LLVM Project, under the Apache License v2.0 with LLVM")
    L.append(" * Exceptions. See https://llvm.org/LICENSE.txt for license information.")
    L.append(" * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception")
    L.append(" *")
    L.append(" *===----------------------------------------------------------------------===*/")
    L.append("")
    L.append("//===----------------------------------------------------------------------===//")
    L.append("// BT06 thin compatibility header.")
    L.append("//")
    L.append("// GENERATED - DO NOT EDIT BY HAND.  Authoritative data:")
    L.append("//   validation/mcs251-dialect/bit-registers.json")
    L.append("// produced by tools/gen-bit-registers.py.  Regenerate this header with:")
    L.append("//   tools/gen-bit-compat-header.py")
    L.append("// In-tree consistency is checked by:")
    L.append("//   tools/gen-bit-registers.py --check-header include/mcs251_bit_compat.h")
    L.append("//")
    L.append("// Scope (BIT-TASK-BREAKDOWN.md BT06): expose only the auditable first safe")
    L.append("// bit-name set through the target builtin __builtin_mcs251_bit_lvalue(ICE).")
    L.append("// Compiler-managed register bits and RSTCFG/XFR control bits hard-error.")
    L.append("// Names whose base SFR is not bit-addressable hard-error too: no hardware")
    L.append("// bit address exists and byte-mask emulation is forbidden, so the official")
    L.append("// header's declaration is answered with an explicit rejection (BT06-1),")
    L.append("// never a silent drop.")
    L.append("// Every remaining official sbit is converted: the BT06-1 reclassification")
    L.append("// moved the peripheral control bits (TCON/SCON/IE/IP and the P3")
    L.append("// alternate-function aliases) into the converted set, so no")
    L.append("// not-in-first-slice bucket remains -- each official name is either")
    L.append("// mapped here or explicitly rejected (no silent downgrade;")
    L.append("// BIT-DECISION-20260911 P09).")
    L.append("//")
    L.append("// This header does NOT define a bare `bit`, `sfr`, or `sbit` keyword and")
    L.append("// does NOT emulate a bit as u8/_Bool or as a byte mask.  Keil `sbit`")
    L.append("// declarations are handled by the -fmcs251-keil frontend, not here.")
    L.append("//")
    L.append("// Approval status: the first safe SFR list is PM's call under")
    L.append("// BIT-DECISION-20260911 P10.  The names below are a CANDIDATE batch (68")
    L.append("// first-slice + 35 BT06-1 reclassified), not an approval.")
    L.append("//===----------------------------------------------------------------------===//")
    L.append("")
    L.append("#ifndef __MCS251_BIT_COMPAT_H__")
    L.append("#define __MCS251_BIT_COMPAT_H__")
    L.append("")
    L.append("// Internal-RAM bits have no Keil name.  b must be an integer constant")
    L.append("// expression in 0..127; the range is enforced at compile time: the")
    L.append("// char[...] operand is an ill-formed negative array size for any b")
    L.append("// outside 0..127 (a _Static_assert cannot appear in this expression")
    L.append("// context).  No addressable pointer is ever produced here.  Fixed-RAM")
    L.append("// backing-byte ownership is BT07 scope, not BT06.")
    L.append("#define MCS251_RAM_BIT(b) \\")
    L.append("    __builtin_mcs251_bit_lvalue( \\")
    L.append("        (b) + 0 * sizeof(char[(b) >= 0 && (b) <= 127 ? 1 : -1]))")
    L.append("")
    L.append("// Reject helpers.  Each rejected name expands to one of these; using it")
    L.append("// anywhere is a hard compile-time error with a specific message.")
    for cls in classes:
        L.append(f"#define MCS251_error_{cls} \\")
        L.append(f'    _Pragma("GCC error \\"{REJECT_MESSAGES[cls]}\\"") MCS251_error_{cls}_marker')
    L.append("")
    L.append("//===----------------------------------------------------------------------===//")
    L.append(f"// Converted candidate batch ({len(approved)} names): GPIO P0..P7, T0/T1")
    L.append("// and UART1 control/status, IE/IP interrupt bits, and the P3")
    L.append("// alternate-function aliases (BT06-1 reclassification).  Mapped to the")
    L.append("// fixed-bit-address builtin.")
    L.append("//===----------------------------------------------------------------------===//")
    for r in approved:
        L.append(f"#define {r['name']:<6s} __builtin_mcs251_bit_lvalue(0x{r['bitaddr']:02X})")
    L.append("")
    L.append("//===----------------------------------------------------------------------===//")
    L.append(f"// Restricted ({len(restricted)} names): any use is a hard error.")
    L.append("//===----------------------------------------------------------------------===//")
    for r in restricted:
        L.append(f"#define {r['name']:<6s} MCS251_error_{reject_class(r['reason'])}")
    L.append("")
    L.append("//===----------------------------------------------------------------------===//")
    L.append(f"// Rejected: non-bit-addressable base ({len(non_bit_addr)} names), the")
    L.append("// declaration from the official header is answered with an explicit")
    L.append("// rejection.  The base SFR address is not a multiple of 8, so no")
    L.append("// hardware bit address exists and byte-mask emulation is forbidden.")
    L.append("//===----------------------------------------------------------------------===//")
    for r in non_bit_addr:
        base = r["base_sfr"]
        base_addr = r["base_sfr_address"]
        bit = r["bit_number"]
        base_txt = f"{base} (0x{base_addr:02X})" if base_addr is not None else "unknown base"
        L.append(f"// {r['name']}: {base}^{bit} -- base {base_txt} is not bit-addressable")
        L.append(f"#define {r['name']:<6s} MCS251_error_non_bit_addressable")
    L.append("")
    L.append("#endif /* __MCS251_BIT_COMPAT_H__ */")
    L.append("")
    return "\n".join(L)


def main() -> int:
    root = Path(__file__).resolve().parents[1]  # validation/mcs251-dialect
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--json", type=Path, default=None, help="bit-registers.json (default: <root>/bit-registers.json)")
    parser.add_argument("-o", "--output", type=Path, default=None, help="header path (default: <root>/include/mcs251_bit_compat.h)")
    args = parser.parse_args()

    root = args.root.resolve()
    json_path = args.json or (root / "bit-registers.json")
    output = args.output or (root / "include" / "mcs251_bit_compat.h")
    if not json_path.is_file():
        print(f"gen-bit-compat-header: error: missing {json_path}", file=sys.stderr)
        return 1

    document = json.loads(json_path.read_text(encoding="utf-8"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(document), encoding="utf-8")
    counts = document["counts"]["by_status"]
    print(
        f"gen-bit-compat-header: wrote {output} "
        f"(approved_pending_pm={counts.get('approved_pending_pm', 0)}, "
        f"restricted={counts.get('restricted', 0)}, "
        f"rejected_non_bit_addressable={counts.get('rejected_non_bit_addressable', 0)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
