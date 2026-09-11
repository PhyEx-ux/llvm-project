#!/usr/bin/env python3
"""Derive ``bit-registers.json`` (BT06) from the porting SFR/sbit reports.

Why this exists
---------------
BT06 needs an auditable, reproducible description of every named bit the Keil
official headers declare, with a *computed* MCS-251 bit address.  The porting
report produced by ``validation/mcs251-porting/tools/sfr-convert.py --report``
records ``sbit name = BYTE ^ bit`` as ``name/byte/bit`` but deliberately does
**not** carry a bit address: the bit-address space is a backend fact, not a
source fact.  This tool closes that gap:

    bit address B = SFR_base_address + bit_index          (SFR space, 0x80..0xFF)

and only when ``SFR_base_address`` is bit-addressable (a multiple of 8).  A
``sbit`` whose base SFR is *not* bit-addressable (e.g. ``sbit SMOD = PCON^7``
with PCON = 0x87) has no hardware bit address.  Emulating it as a byte mask is
explicitly forbidden by BT06, so such entries are classified ``unsupported``
and are never emitted as a bit lvalue.

Bit-address map (authoritative rules)
-------------------------------------
``llvm/lib/Target/MCS251/MCS251InstrInfo.td:72-80`` and
``llvm/lib/Target/MCS251/MCTargetDesc/MCS251BitAddr.h:52-60``:

  * RAM bits   B in [0, 127]  -> backing byte 0x20 + (B >> 3), bit index B & 7
  * SFR  bits  B in [128,255] -> backing byte B & 0xF8,        bit index B & 7

Two documented special cases are covered by the unit tests in
``tests/test-bit-compat.py``: 0xD7 is PSW.CY, and bit 0xFF backs onto byte
0xF8 (not the direct byte at 0xFF).

Status policy (NOT a claim of approval)
---------------------------------------
``approved_pending_pm``  candidate first batch.  Chosen from the QEMU-backed
                         device set (GPIO P0..P7, timer T0/T1 flags, UART1
                         status) documented in
                         ``validation/mcs251-dialect/DIALECT-PACKAGE.md`` §0 and
                         §2.3.  Per BIT-DECISION-20260911 P10 the first safe
                         SFR list is *PM's* call, so this status is a proposal,
                         never an approval.
``restricted``           mandated rejection: EA, the ACC/B/PSW compiler-managed
                         register bits and the RSTCFG-related bits (P04), plus
                         EAXFR.  The compat header must hard-error on these.
``unsupported``          everything else: no model in the first slice, or a
                         non-bit-addressable base (no legal bit address).

Output determinism
------------------
``generated_utc`` is ``"unspecified"`` by default so that two runs on the same
inputs produce byte-identical JSON; pass ``--generated-utc`` to stamp a date.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

FORMAT = "mcs251-bit-registers-v1"
VERSION = 1

# ---------------------------------------------------------------------------
# Bit-address synthesis (single source of truth, shared with the test suite).
# ---------------------------------------------------------------------------

RAM_BIT_MAX = 0x7F          # B in [0, 127] is internal-RAM bit space
SFR_BIT_MIN = 0x80          # B in [128, 255] is SFR bit space
BITADDR_MAX = 0xFF
RAM_BACKING_BASE = 0x20     # RAM bit 0 is the first bit of byte 0x20


def ram_backing_byte(bitaddr: int) -> int:
    """Backing byte of a RAM bit address (B in [0, 127])."""
    return RAM_BACKING_BASE + (bitaddr >> 3)


def sfr_backing_byte(bitaddr: int) -> int:
    """Backing byte of an SFR bit address (B in [128, 255])."""
    return bitaddr & 0xF8


def backing_byte(bitaddr: int) -> int:
    """Backing byte for any legal bit address in [0, 255]."""
    if not 0 <= bitaddr <= BITADDR_MAX:
        raise ValueError(f"bit address {bitaddr} out of range [0, 255]")
    if bitaddr <= RAM_BIT_MAX:
        return ram_backing_byte(bitaddr)
    return sfr_backing_byte(bitaddr)


def bit_index(bitaddr: int) -> int:
    """Bit index within the backing byte (always 0..7)."""
    if not 0 <= bitaddr <= BITADDR_MAX:
        raise ValueError(f"bit address {bitaddr} out of range [0, 255]")
    return bitaddr & 7


def synth_bitaddr(base_address: int | None, index: int | None) -> int | None:
    """Synthesize a bit address from an SFR byte address and a bit index.

    Returns ``None`` when no legal MCS-251 bit address exists: the base SFR
    must be bit-addressable (address a multiple of 8) and the index must be in
    0..7.  ``None`` is a loud "cannot express as a hardware bit" -- callers must
    never fall back to a byte/mask spelling.
    """
    if base_address is None or index is None:
        return None
    if not 0 <= index <= 7:
        return None
    if base_address % 8 != 0:
        return None
    bitaddr = base_address + index
    if not SFR_BIT_MIN <= bitaddr <= BITADDR_MAX:
        return None
    return bitaddr


# ---------------------------------------------------------------------------
# Policy tables.  These are the only place BT06 decides status; everything is
# data so a reviewer can diff the intent, not infer it from control flow.
# ---------------------------------------------------------------------------

def _gpio_names() -> set[str]:
    # P00..P77: port Pn bit b is named Pnb in the STC headers.
    return {f"P{port}{bit}" for port in range(8) for bit in range(8)}


# Candidate first batch: QEMU-backed GPIO plus the UART1/timer status flags.
APPROVED_NAMES = _gpio_names() | {"TF0", "TF1", "TI", "RI"}

RESTRICTED_REASON = {
    # P04: EA (0xAF) is the global interrupt enable; ordinary access rejected.
    "EA": "P04: EA is a compiler-managed interrupt-enable bit; ordinary access rejected",
    "EAXFR": "P04/BT06: EAXFR is an XFR-enable control bit; auto-enabling is out of scope",
}
for _i in range(8):
    RESTRICTED_REASON[f"ACC{_i}"] = (
        f"P04: ACC{_i} is an accumulator bit (compiler-managed); ordinary access rejected"
    )
    RESTRICTED_REASON[f"B{_i}"] = (
        f"P04: B{_i} is a B-register bit (compiler-managed); ordinary access rejected"
    )
# PSW bits (0xD0..0xD7), including the 0xD7 == PSW.CY special case.
for _n, _r in {
    "P": "P04: PSW.P (parity) is compiler-managed; ordinary access rejected",
    "F1": "P04: PSW.F1 is compiler-managed; ordinary access rejected",
    "OV": "P04: PSW.OV is compiler-managed; ordinary access rejected",
    "RS0": "P04: PSW.RS0 selects the register bank; ordinary access rejected",
    "RS1": "P04: PSW.RS1 selects the register bank; ordinary access rejected",
    "F0": "P04: PSW.F0 is compiler-managed; ordinary access rejected",
    "AC": "P04: PSW.AC is compiler-managed; ordinary access rejected",
    "CY": "P04: PSW.CY (bit address 0xD7) is compiler-managed; ordinary access rejected",
}.items():
    RESTRICTED_REASON[_n] = _r
# PSW1 shadow status bits.
RESTRICTED_REASON["N"] = "P04: PSW1.N is a shadow status bit; ordinary access rejected"
RESTRICTED_REASON["Z"] = "P04: PSW1.Z is a shadow status bit; ordinary access rejected"
# RSTCFG-related bits (RSTCFG itself is the AS6-forbidden 0xFF byte).
RESTRICTED_REASON["ENLVR"] = (
    "P04/BT06: ENLVR is an RSTCFG-related bit; RSTCFG is forbidden in AS6 and "
    "has no bit-addressable base"
)
RESTRICTED_REASON["P54RST"] = (
    "P04/BT06: P54RST is an RSTCFG-related bit; RSTCFG is forbidden in AS6 and "
    "has no bit-addressable base"
)

APPROVED_OPS = ["read", "write01", "toggle"]

STATUS_LEGEND = {
    "approved_pending_pm": (
        "Candidate first batch (QEMU-backed GPIO / UART1 / T0-T1 status). "
        "Pending PM approval under BIT-DECISION-20260911 P10 -- this is a "
        "proposal, not an approval."
    ),
    "restricted": (
        "Mandated rejection: EA / ACC / B / PSW compiler-managed register bits "
        "and RSTCFG-related bits (P04). The compat header must hard-error."
    ),
    "unsupported": (
        "Not in the first slice, or no legal bit address (non-bit-addressable "
        "base). No byte-mask emulation is provided."
    ),
}


def classify(name: str, bitaddr: int | None, base_address: int | None) -> tuple[str, str | None]:
    """Return ``(status, reason)`` for a named bit.  Never returns approved for
    an unknown name and never silently downgrades to a byte/AS0 spelling."""
    if name in RESTRICTED_REASON:
        return "restricted", RESTRICTED_REASON[name]
    if bitaddr is None:
        if base_address is None:
            return "unsupported", "base SFR not found in report direct_sfr list"
        return (
            "unsupported",
            f"non_bit_addressable_base: base SFR address 0x{base_address:02X} is "
            "not a multiple of 8, so no hardware bit address exists",
        )
    if name in APPROVED_NAMES:
        return "approved_pending_pm", None
    return "unsupported", "not_in_first_slice_pending_pm"


# ---------------------------------------------------------------------------
# Report ingestion.
# ---------------------------------------------------------------------------

def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_report(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("format") != "mcs251-sfr-convert-v1":
        raise SystemExit(f"{path}: unexpected report format {data.get('format')!r}")
    return data


def build_records(report_paths: list[Path], root: Path) -> tuple[list[dict], list[dict]]:
    """Merge one or more reports into a deterministic record list.

    Same name + same bit address in several reports is an alias and is folded
    into one record.  Same name + *different* bit address is a hard error: the
    two sources disagree about the hardware, and guessing is not allowed.
    """
    records: dict[str, dict] = {}
    generated_from: list[dict] = []
    repo_root = root.parent.parent  # validation/mcs251-dialect -> MCS251

    for report_path in report_paths:
        report = load_report(report_path)
        direct = {e["name"]: e["address"] for e in report.get("direct_sfr", [])}
        try:
            rel_report = report_path.relative_to(repo_root).as_posix()
        except ValueError:
            rel_report = report_path.name
        generated_from.append(
            {
                "report": rel_report,
                "report_sha256": sha256_file(report_path),
                # The upstream Keil header was not present in this working tree,
                # so its hash is recorded transitively from the report that
                # computed it in the original corpus environment.
                "source": report.get("source"),
                "source_sha256_from_report": report.get("source_sha256"),
            }
        )

        for entry in report.get("sbit", []):
            name = entry["name"]
            base_name = entry.get("byte")
            base_address = direct.get(base_name)
            bitaddr = synth_bitaddr(base_address, entry.get("bit"))
            status, reason = classify(name, bitaddr, base_address)
            record = {
                "name": name,
                "bitaddr": bitaddr,
                "backing_byte": backing_byte(bitaddr) if bitaddr is not None else None,
                "bit_index": bit_index(bitaddr) if bitaddr is not None else None,
                "base_sfr": base_name,
                "base_sfr_address": base_address,
                "bit_number": entry.get("bit"),
                "allowed_ops": list(APPROVED_OPS) if status == "approved_pending_pm" else [],
                "status": status,
                "reason": reason,
                "source": {
                    "report": rel_report,
                    "file": report.get("source"),
                    "source_sha256_from_report": report.get("source_sha256"),
                    "line": entry.get("line"),
                },
            }
            existing = records.get(name)
            if existing is None:
                records[name] = record
            elif existing["bitaddr"] != bitaddr:
                raise SystemExit(
                    f"conflicting bit address for {name!r}: "
                    f"{existing['bitaddr']} vs {bitaddr}"
                )

    ordered = sorted(
        records.values(),
        key=lambda r: (r["bitaddr"] is None, r["bitaddr"] if r["bitaddr"] is not None else 0, r["name"]),
    )
    return ordered, generated_from


def build_document(records: list[dict], generated_from: list[dict], generated_utc: str) -> dict:
    counts: dict[str, int] = {}
    for record in records:
        counts[record["status"]] = counts.get(record["status"], 0) + 1
    return {
        "format": FORMAT,
        "version": VERSION,
        "generated_utc": generated_utc,
        "generated_by": "validation/mcs251-dialect/tools/gen-bit-registers.py",
        "generated_from": generated_from,
        "policy": {
            "approved_note": (
                "status approved_pending_pm is a *candidate* first batch; the "
                "first safe SFR list is PM's call under BIT-DECISION-20260911 "
                "P10 and is NOT approved here."
            ),
            "restricted_decision": (
                "BIT-DECISION-20260911.md P03/P04; also BIT-TASK-BREAKDOWN.md "
                "BT06 lines 264-277."
            ),
            "no_silent_downgrade": (
                "Records that are not approved are never mapped to a byte, a "
                "mask, or an AS0 lvalue. Unknown/unapproved names stay "
                "undeclared and therefore fail loudly at compile time."
            ),
        },
        "bit_address_rules": {
            "ram_bit": "B in [0,127]: backing_byte = 0x20 + (B >> 3), bit_index = B & 7",
            "sfr_bit": "B in [128,255]: backing_byte = B & 0xF8, bit_index = B & 7",
            "sfr_base_synthesis": (
                "given a bit-addressable SFR byte A (A %% 8 == 0) and n in 0..7: B = A + n"
            ),
            "special_cases": [
                "bit 0xD7 == PSW.CY (PSW = 0xD0, bit 7)",
                "bit 0xFF backs onto byte 0xF8, not the direct byte at 0xFF",
            ],
            "authority": [
                "llvm/lib/Target/MCS251/MCS251InstrInfo.td:72-80",
                "llvm/lib/Target/MCS251/MCTargetDesc/MCS251BitAddr.h:52-60",
                "llvm/lib/Target/MCS251/MCS251ISelLowering.cpp:783-793,795-809",
            ],
        },
        "status_legend": STATUS_LEGEND,
        "ram_bit_space": {
            "bitaddr_range": [0, 127],
            "backing_byte_range": [0x20, 0x2F],
            "status": "approved_pending_pm",
            "allowed_ops": list(APPROVED_OPS),
            "header_macro": "MCS251_RAM_BIT(b)",
            "note": (
                "No Keil name exists for internal-RAM bits, so they are exposed "
                "through the MCS251_RAM_BIT(b) macro with an ICE b in 0..127. "
                "backing-byte ownership for fixed RAM bits is BT07's scope, not "
                "BT06's."
            ),
        },
        "counts": {"total": len(records), "by_status": counts},
        "records": records,
    }


# ---------------------------------------------------------------------------
# Header consistency check (shared with tests/test-bit-compat.py).
# ---------------------------------------------------------------------------

BIT_MACRO_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(?P<name>[A-Za-z_]\w*)[ \t]+"
    r"__builtin_mcs251_bit_lvalue\([ \t]*(?P<addr>0[xX][0-9A-Fa-f]+|\d+)[ \t]*\)[ \t]*$",
    re.MULTILINE,
)
REJECT_MACRO_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(?P<name>[A-Za-z_]\w*)[ \t]+"
    r"(?P<body>MCS251_error_[A-Za-z0-9_]+)[ \t]*$",
    re.MULTILINE,
)


def check_header(document: dict, header_text: str) -> list[str]:
    """Return a list of consistency problems between the JSON and the header.

    An empty list means: every bit macro in the header matches an approved
    record's bit address, every approved record has a header macro, every
    restricted record has an explicit reject macro, and no unapproved name is
    mapped to the builtin.
    """
    problems: list[str] = []
    by_name = {r["name"]: r for r in document["records"]}

    header_bits = {m.group("name"): int(m.group("addr"), 0) for m in BIT_MACRO_RE.finditer(header_text)}
    header_rejects = {m.group("name"): m.group("body") for m in REJECT_MACRO_RE.finditer(header_text)}

    # 1. Every builtin-mapped name must be an approved record with the same address.
    for name, addr in header_bits.items():
        record = by_name.get(name)
        if record is None:
            problems.append(f"header maps unknown name {name} to builtin (no JSON record)")
        elif record["status"] != "approved_pending_pm":
            problems.append(f"header maps {name} to builtin but JSON status is {record['status']}")
        elif record["bitaddr"] != addr:
            problems.append(
                f"header {name} ICE 0x{addr:02X} != JSON bitaddr "
                f"{'None' if record['bitaddr'] is None else format(record['bitaddr'], '#04X')}"
            )

    # 2. Every approved record must be present in the header.
    for record in document["records"]:
        if record["status"] == "approved_pending_pm" and record["name"] not in header_bits:
            problems.append(f"JSON approved record {record['name']} missing from header")

    # 3. Every restricted record must have an explicit reject macro (and not a builtin).
    for record in document["records"]:
        if record["status"] != "restricted":
            continue
        name = record["name"]
        if name in header_bits:
            problems.append(f"restricted name {name} must not be a builtin bit lvalue in the header")
        if name not in header_rejects:
            problems.append(f"restricted name {name} missing explicit reject macro in header")

    # 4. Reject macros must only be used for restricted records.
    for name in header_rejects:
        record = by_name.get(name)
        if record is None:
            problems.append(f"header rejects unknown name {name} (no JSON record)")
        elif record["status"] != "restricted":
            problems.append(f"header rejects {name} but JSON status is {record['status']}")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default_root = Path(__file__).resolve().parents[1]  # validation/mcs251-dialect
    parser.add_argument("--root", type=Path, default=default_root, help="validation/mcs251-dialect")
    parser.add_argument(
        "--report",
        type=Path,
        action="append",
        default=None,
        help="porting report JSON (repeatable); default: both stc32g* reports",
    )
    parser.add_argument("-o", "--output", type=Path, default=None, help="write JSON here")
    parser.add_argument("--generated-utc", default="unspecified", help="timestamp string (default: unspecified)")
    parser.add_argument("--check-header", type=Path, default=None, help="verify a header against the JSON and exit")
    args = parser.parse_args()

    root = args.root.resolve()
    if args.report:
        reports = [p.resolve() for p in args.report]
    else:
        reports_dir = root.parent / "mcs251-porting" / "reports"
        reports = [
            reports_dir / "stc32g144k246-v1-report.json",
            reports_dir / "stc32g-v1-report.json",
        ]
    missing = [str(p) for p in reports if not p.is_file()]
    if missing:
        print(f"gen-bit-registers: error: missing report(s): {', '.join(missing)}", file=sys.stderr)
        return 1

    records, generated_from = build_records(reports, root)
    document = build_document(records, generated_from, args.generated_utc)

    if args.check_header is not None:
        problems = check_header(document, args.check_header.read_text(encoding="utf-8"))
        for problem in problems:
            print(f"gen-bit-registers: HEADER MISMATCH: {problem}", file=sys.stderr)
        if problems:
            return 1
        print(f"gen-bit-registers: header {args.check_header} consistent with JSON")
        return 0

    output = args.output or (root / "bit-registers.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    counts = document["counts"]["by_status"]
    print(
        f"gen-bit-registers: wrote {output} "
        f"({document['counts']['total']} records; "
        f"approved_pending_pm={counts.get('approved_pending_pm', 0)}, "
        f"restricted={counts.get('restricted', 0)}, "
        f"unsupported={counts.get('unsupported', 0)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
