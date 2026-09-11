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
explicitly forbidden by BT06, so such entries are classified
``rejected_non_bit_addressable`` and are emitted in the compat header as an
explicit reject macro plus a comment, never as a bit lvalue.

Official coverage baseline (BT06-1)
-----------------------------------
Besides the porting reports, this tool ingests the official Keil DEMO header
directly (``--official-header``, default
``STC32G144K246-DEMO-CODE/COMM/STC32G144K246.H``).  Every ``sbit`` declared
there must end up with an explicit record: the report corpus was derived from
a different header copy and misses 13 official names (all with non-bit-
addressable bases), which previously made the JSON cover only 330/343 official
names.  The generator merges both sources, refuses on conflicting addresses,
and writes a computed ``official_coverage`` block into the JSON so the
343/343 traceability claim is reproducible from the artifact itself.

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
``approved_pending_pm``  converted set: the 68-name first slice (QEMU-backed
                         GPIO P0..P7, timer T0/T1 flags, UART1 status) plus
                         the 35 names reclassified by the BT06-1 ruling (see
                         ``BT06_RECLASSIFIED_NAMES`` below).  Per
                         BIT-DECISION-20260911 P10 the first safe SFR list is
                         *PM's* call, so this status is a proposal, never an
                         approval.
``restricted``           mandated rejection: EA, the ACC/B/PSW compiler-managed
                         register bits and the RSTCFG-related bits (P04), plus
                         EAXFR.  The compat header must hard-error on these.
``rejected_non_bit_addressable``
                         the base SFR address is not a multiple of 8, so no
                         hardware bit address exists (e.g. SMOD = PCON^7 with
                         PCON = 0x87).  BT06 forbids byte-mask emulation, so
                         the compat header hard-errors on these too (reject
                         macro plus a comment carrying the reason).

There is deliberately **no fourth bucket**.  The former ``unsupported`` /
``not_in_first_slice_pending_pm`` classification was removed by the BT06-1
ruling on the Alice re-audit: "not in the first slice" is not a technical
rejection reason for a bit-addressable peripheral control bit.  A name that
does not fall into one of the three statuses above makes generation fail
loudly instead of being parked in a deleted bucket.

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
FIRST_SLICE_NAMES = _gpio_names() | {"TF0", "TF1", "TI", "RI"}

# BT06-1 reclassification (35 names, coordinator ruling on the Alice re-audit
# 2026-09-11): the remaining official sbits whose bases are bit-addressable.
# They are peripheral control bits -- ordinary SFR bit reads/writes with no
# CPU state the compiler has to model -- so "not_in_first_slice_pending_pm"
# was struck down as a rejection reason and they are converted like the first
# slice.  The P04 management class (EA, the PSW/ACC/B families, RSTCFG bits,
# EAXFR) is *not* touched: those 30 names stay restricted.
BT06_RECLASSIFIED_NAMES = {
    # TCON (0x88): timer control.
    "TR0", "TR1", "IE0", "IE1", "IT0", "IT1",
    # SCON (0x98): UART1 control.
    "SM0", "SM1", "SM2", "REN", "TB8", "RB8",
    # IE (0xA8): interrupt enable.
    "EX0", "ET0", "EX1", "ET1", "ES", "EADC", "ELVD",
    # IP (0xB8): interrupt priority.
    "PX0", "PT0", "PX1", "PT1", "PS", "PADC", "PLVD", "PPCA",
    # P3 (0xB0) alternate-function aliases of the already-supported P37..P30.
    "RD", "WR", "T1", "T0", "INT1", "INT0", "TXD", "RXD",
}

CONVERTED_NAMES = FIRST_SLICE_NAMES | BT06_RECLASSIFIED_NAMES

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
        "Converted set: 68 first-slice names (QEMU-backed GPIO / UART1 / T0-T1 "
        "status) plus the 35 BT06-1 reclassified peripheral control bits "
        "(TCON/SCON/IE/IP and the P3 alternate-function aliases). Pending PM "
        "approval under BIT-DECISION-20260911 P10 -- this is a proposal, not "
        "an approval."
    ),
    "restricted": (
        "Mandated rejection: EA / ACC / B / PSW compiler-managed register bits "
        "and RSTCFG-related bits (P04). The compat header must hard-error."
    ),
    "rejected_non_bit_addressable": (
        "The base SFR is not bit-addressable (address not a multiple of 8), so "
        "no hardware bit address exists. Byte-mask emulation is forbidden, so "
        "the compat header hard-errors on these names too."
    ),
}


def classify(name: str, bitaddr: int | None, base_address: int | None) -> tuple[str, str | None]:
    """Return ``(status, reason)`` for a named bit.  Never returns approved for
    an unknown name and never silently downgrades to a byte/AS0 spelling.

    The former ``unsupported`` / ``not_in_first_slice_pending_pm`` bucket is
    gone (BT06-1 ruling): a bit-addressable non-management name is either on
    the converted list or generation fails loudly.  There is no third bucket
    to park a name in, so this function either returns one of the three
    statuses above or raises ``SystemExit`` with the reason.
    """
    if name in RESTRICTED_REASON:
        return "restricted", RESTRICTED_REASON[name]
    if bitaddr is None:
        if base_address is None:
            raise SystemExit(
                f"sbit {name!r}: base SFR not found in the report direct_sfr "
                "list, so no bit address can be synthesized; refusing to "
                "classify (the former 'unsupported' bucket no longer exists)"
            )
        if base_address % 8 != 0:
            return (
                "rejected_non_bit_addressable",
                f"non_bit_addressable_base: base SFR address 0x{base_address:02X} is "
                "not a multiple of 8, so no hardware bit address exists",
            )
        raise SystemExit(
            f"sbit {name!r}: base SFR address 0x{base_address:02X} is "
            "bit-addressable but no legal bit address synthesizes from it; "
            "refusing to classify (the former 'unsupported' bucket no longer "
            "exists)"
        )
    if name in CONVERTED_NAMES:
        return "approved_pending_pm", None
    raise SystemExit(
        f"sbit {name!r}: convertible bit address 0x{bitaddr:02X} but the name "
        "is on neither the first-slice nor the BT06-1 reclassification list; "
        "refusing to classify (the former 'unsupported' bucket no longer "
        "exists, and the converted set must not silently grow)"
    )


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


OFFICIAL_SFR_RE = re.compile(r"^\s*sfr\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)\s*;", re.MULTILINE)
OFFICIAL_SBIT_BASE_RE = re.compile(r"^\s*sbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*(\d+)\s*;", re.MULTILINE)
OFFICIAL_SBIT_DIRECT_RE = re.compile(r"^\s*sbit\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)\s*;", re.MULTILINE)


def parse_official_header(path: Path) -> dict:
    """Parse ``sfr``/``sbit`` declarations straight from a Keil header.

    Returns ``{"sfr": {name: address}, "sbit": [{"name", "byte", "bit",
    "line"}, ...]}``.  Used as the official coverage baseline (BT06-1): the
    porting reports were derived from a different header copy and miss some
    official ``sbit`` names, so the generator merges this parse in rather than
    trusting the corpus copy for completeness.
    """
    text = path.read_text(encoding="latin1")
    sfr = {m.group(1): int(m.group(2), 0) for m in OFFICIAL_SFR_RE.finditer(text)}
    sbit: list[dict] = []
    for m in OFFICIAL_SBIT_BASE_RE.finditer(text):
        sbit.append(
            {"name": m.group(1), "byte": m.group(2), "bit": int(m.group(3)), "line": text.count("\n", 0, m.start()) + 1}
        )
    base_names = {e["name"] for e in sbit}
    for m in OFFICIAL_SBIT_DIRECT_RE.finditer(text):
        if m.group(1) in base_names:
            raise SystemExit(f"{path}: sbit {m.group(1)} declared twice with different forms")
        sbit.append(
            {"name": m.group(1), "byte": None, "bit": None, "address": int(m.group(2), 0),
             "line": text.count("\n", 0, m.start()) + 1}
        )
    dupes = sorted(n for n in sfr if n in base_names)
    if dupes:
        raise SystemExit(f"{path}: names declared both sfr and sbit: {', '.join(dupes)}")
    return {"sfr": sfr, "sbit": sbit}


def build_records(
    report_paths: list[Path], root: Path, official_header: Path | None
) -> tuple[list[dict], list[dict], dict | None]:
    """Merge one or more reports plus the official header into a deterministic
    record list.

    Same name + same bit address in several sources is an alias and is folded
    into one record.  Same name + *different* bit address is a hard error: the
    two sources disagree about the hardware, and guessing is not allowed.
    """
    records: dict[str, dict] = {}
    generated_from: list[dict] = []
    repo_root = root.parent.parent  # validation/mcs251-dialect -> MCS251
    official_info: dict | None = None

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

    if official_header is not None:
        parsed = parse_official_header(official_header)
        official_sha = sha256_file(official_header)
        direct = parsed["sfr"]
        for entry in parsed["sbit"]:
            name = entry["name"]
            base_name = entry.get("byte")
            base_address = direct.get(base_name)
            bitaddr = (
                entry["address"]
                if entry.get("address") is not None
                else synth_bitaddr(base_address, entry.get("bit"))
            )
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
                    "official_header": official_header.name,
                    "official_header_sha256": official_sha,
                    "line": entry["line"],
                },
            }
            existing = records.get(name)
            if existing is None:
                records[name] = record
            elif existing["bitaddr"] != bitaddr:
                raise SystemExit(
                    f"conflicting bit address for {name!r} between "
                    f"{existing['source'].get('report', 'official header')} and the "
                    f"official header {official_header}: "
                    f"{existing['bitaddr']} vs {bitaddr}"
                )
        official_info = {
            "file": official_header.name,
            "sha256": official_sha,
            "sbit_names": [e["name"] for e in parsed["sbit"]],
        }
        generated_from.append(
            {
                "official_header": official_header.name,
                "official_header_sha256": official_sha,
                "role": "official sbit coverage baseline (BT06-1)",
            }
        )

    ordered = sorted(
        records.values(),
        key=lambda r: (r["bitaddr"] is None, r["bitaddr"] if r["bitaddr"] is not None else 0, r["name"]),
    )
    return ordered, generated_from, official_info


def build_official_coverage(records: list[dict], official_info: dict | None) -> dict | None:
    """Compute the BT06-1 official coverage block and enforce its invariant.

    Every ``sbit`` name declared by the official header must be present as a
    record; the generator refuses to emit a JSON that does not cover the
    official set.  The block accounts for each name exactly once: converted
    (approved) plus explicitly rejected (restricted or non-bit-addressable)
    must equal the official total.  There is no "recorded not-in-first-slice"
    bucket any more (BT06-1 ruling).
    """
    if official_info is None:
        return None
    by_name = {r["name"]: r for r in records}
    official_names = list(dict.fromkeys(official_info["sbit_names"]))
    missing = [n for n in official_names if n not in by_name]
    if missing:
        raise SystemExit(
            "official header sbit names without a JSON record (BT06-1 "
            f"coverage invariant violated): {', '.join(missing)}"
        )
    buckets = {
        "converted": "approved_pending_pm",
        "rejected_restricted": "restricted",
        "rejected_non_bit_addressable": "rejected_non_bit_addressable",
    }
    counts = {label: 0 for label in buckets}
    unexpected: list[str] = []
    for n in official_names:
        status = by_name[n]["status"]
        for label, want in buckets.items():
            if status == want:
                counts[label] += 1
                break
        else:
            unexpected.append(f"{n}:{status}")
    if unexpected:
        raise SystemExit(f"official names with unclassifiable status: {', '.join(unexpected)}")
    converted = counts["converted"]
    rejected = sum(v for k, v in counts.items() if k != "converted")
    if converted + rejected != len(official_names):
        raise SystemExit("internal: coverage buckets do not sum to the official total")
    return {
        "official_header": {"file": official_info["file"], "sha256": official_info["sha256"]},
        "official_sbit_count": len(official_names),
        "recorded": len(official_names) - len(missing),
        "converted": converted,
        "explicitly_rejected": rejected,
        "by_bucket": counts,
        "note": (
            "BT06-1: every official sbit is either converted (first slice or "
            "the BT06-1 reclassified peripheral control bits) or explicitly "
            "rejected with a recorded reason (restricted per P04, "
            "non-bit-addressable base); converted + rejected == official "
            "total. The former not-in-first-slice bucket was removed."
        ),
    }


def build_document(
    records: list[dict], generated_from: list[dict], generated_utc: str, official_info: dict | None
) -> dict:
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
                "status approved_pending_pm is the *converted* set: 68 "
                "first-slice names plus the 35 BT06-1 reclassified peripheral "
                "control bits. The first safe SFR list is PM's call under "
                "BIT-DECISION-20260911 P10 and is NOT approved here."
            ),
            "restricted_decision": (
                "BIT-DECISION-20260911.md P03/P04; also BIT-TASK-BREAKDOWN.md "
                "BT06 lines 264-277."
            ),
            "no_silent_downgrade": (
                "Records that are not converted are never mapped to a byte, a "
                "mask, or an AS0 lvalue. The former 'unsupported' / "
                "'not_in_first_slice_pending_pm' bucket was removed by the "
                "BT06-1 ruling; a name that fits no status aborts generation."
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
        "official_coverage": build_official_coverage(records, official_info),
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

DIRECTIVE_LINE_RE = re.compile(r"^[ \t]*#[ \t]*(?P<dir>define|undef)\b(?P<rest>.*)$")
DEFINE_NAME_RE = re.compile(r"^(?P<name>[A-Za-z_]\w*)(?P<body>.*)$", re.DOTALL)
BUILTIN_CALL_RE = re.compile(
    r"^__builtin_mcs251_bit_lvalue\([ \t]*(?P<addr>0[xX][0-9A-Fa-f]+|\d+)[ \t]*\)$", re.DOTALL
)
REJECT_BODY_RE = re.compile(r"^(?P<body>MCS251_error_[A-Za-z0-9_]+)$")


def strip_balanced_parens(text: str) -> str:
    """Strip whitespace and fully balanced outer parentheses, repeatedly.

    ``(__builtin_mcs251_bit_lvalue(0x80))`` normalizes to the bare call so a
    paren-wrapped mapping cannot evade the checker (BT06-3).
    """
    out = text.strip()
    while out.startswith("(") and out.endswith(")"):
        depth = 0
        balanced = True
        for i, ch in enumerate(out):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0 and i != len(out) - 1:
                    balanced = False
                    break
        if not balanced:
            break
        out = out[1:-1].strip()
    return out


def parse_header_defs(header_text: str) -> tuple[dict[str, dict], list[str]]:
    """Simulate the object-like macro definitions of a header.

    Handles backslash line continuations (line numbers stay physical), 
    ``#undef``, and redefinition, and returns ``(effective, problems)`` where
    ``effective[name]`` is the final ``{"body", "line"}`` of each name.
    Redefinition and ``#undef`` of an already-defined name are reported as
    problems: the generated header never does either, so their presence means
    someone is shadowing a mapping (Alice's ``#undef EA`` + paren-wrapped
    builtin mutant class).
    """
    effective: dict[str, dict] = {}
    problems: list[str] = []
    phys = header_text.splitlines()
    i = 0
    while i < len(phys):
        start_line = i + 1
        logical = phys[i]
        while logical.rstrip().endswith("\\") and i + 1 < len(phys):
            logical = logical.rstrip()[:-1] + " " + phys[i + 1]
            i += 1
        i += 1
        m = DIRECTIVE_LINE_RE.match(logical)
        if m is None:
            continue
        directive = m.group("dir")
        rest = m.group("rest").strip()
        if directive == "undef":
            parts = rest.split()
            name = parts[0] if parts else ""
            if name in effective:
                problems.append(f"line {start_line}: #undef {name} removes an existing mapping")
                del effective[name]
            continue
        dm = DEFINE_NAME_RE.match(rest)
        if dm is None or dm.group("name") == "" or dm.group("body").startswith("("):
            # function-style macro: not an object-like bit mapping
            continue
        name = dm.group("name")
        body = dm.group("body").strip()
        if name in effective:
            problems.append(f"line {start_line}: #define {name} redefines an existing mapping")
        effective[name] = {"body": body, "line": start_line}
    return effective, problems


def classify_header_defs(header_text: str) -> tuple[dict[str, int], dict[str, str], list[str]]:
    """Split effective macro definitions into builtin/reject mappings.

    Any definition whose (paren-normalized) body mentions the builtin must be
    exactly the canonical spelling ``#define NAME
    __builtin_mcs251_bit_lvalue(0xNN)`` -- no surrounding parentheses, no
    composition, no arithmetic.  The generated header only ever emits the
    canonical spelling, so a paren-wrapped or disguised mapping is a problem
    even when its address happens to match; this is what catches the mutant
    classes the old single-line regex silently ignored (BT06-3).
    """
    effective, problems = parse_header_defs(header_text)
    header_bits: dict[str, int] = {}
    header_rejects: dict[str, str] = {}
    for name, info in effective.items():
        body = info["body"]
        normalized = strip_balanced_parens(body)
        if "__builtin_mcs251_bit_lvalue" in normalized:
            bm = BUILTIN_CALL_RE.match(normalized)
            if bm is None:
                problems.append(
                    f"line {info['line']}: {name} maps to the bit builtin in a "
                    f"disguised way: {normalized!r}"
                )
            elif BUILTIN_CALL_RE.match(body) is None:
                problems.append(
                    f"line {info['line']}: {name} maps to the bit builtin in a "
                    "non-canonical spelling (must be exactly "
                    f"__builtin_mcs251_bit_lvalue(0xNN)): {body!r}"
                )
            if bm is not None:
                header_bits[name] = int(bm.group("addr"), 0)
            continue
        rm = REJECT_BODY_RE.match(normalized)
        if rm is not None:
            header_rejects[name] = rm.group("body")
    return header_bits, header_rejects, problems


REJECT_STATUSES = ("restricted", "rejected_non_bit_addressable")


def check_header(document: dict, header_text: str) -> list[str]:
    """Return a list of consistency problems between the JSON and the header.

    An empty list means: every bit macro in the header matches an approved
    record's bit address, every approved record has a header macro, every
    restricted or non-bit-addressable record has an explicit reject macro, no
    unapproved name is mapped to the builtin, and the header contains no
    ``#undef``/redefinition shadowing of any mapping.
    """
    problems: list[str] = []
    by_name = {r["name"]: r for r in document["records"]}

    header_bits, header_rejects, parse_problems = classify_header_defs(header_text)
    problems.extend(parse_problems)

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

    # 3. Every explicitly rejected record must have a reject macro (and not a builtin).
    for record in document["records"]:
        if record["status"] not in REJECT_STATUSES:
            continue
        name = record["name"]
        if name in header_bits:
            problems.append(
                f"{record['status']} name {name} must not be a builtin bit lvalue in the header"
            )
        if name not in header_rejects:
            problems.append(f"{record['status']} name {name} missing explicit reject macro in header")

    # 4. Reject macros must only be used for explicitly rejected records.
    for name in header_rejects:
        record = by_name.get(name)
        if record is None:
            problems.append(f"header rejects unknown name {name} (no JSON record)")
        elif record["status"] not in REJECT_STATUSES:
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
    parser.add_argument(
        "--official-header",
        type=Path,
        default=Path("/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/COMM/STC32G144K246.H"),
        help=(
            "official Keil header used as the 343-sbit coverage baseline "
            "(BT06-1); required, there is no partial-coverage mode"
        ),
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

    official_header = args.official_header.resolve() if args.official_header is not None else None
    if official_header is None or not official_header.is_file():
        print(
            "gen-bit-registers: error: official header not found: "
            f"{official_header}; pass --official-header (BT06-1 requires full "
            "official sbit coverage, there is no partial mode)",
            file=sys.stderr,
        )
        return 1

    records, generated_from, official_info = build_records(reports, root, official_header)
    document = build_document(records, generated_from, args.generated_utc, official_info)

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
    cov = document.get("official_coverage") or {}
    print(
        f"gen-bit-registers: wrote {output} "
        f"({document['counts']['total']} records; "
        f"approved_pending_pm={counts.get('approved_pending_pm', 0)}, "
        f"restricted={counts.get('restricted', 0)}, "
        f"rejected_non_bit_addressable={counts.get('rejected_non_bit_addressable', 0)}; "
        f"official coverage "
        f"{cov.get('recorded')}/{cov.get('official_sbit_count')} = "
        f"converted {cov.get('converted')} + explicitly rejected {cov.get('explicitly_rejected')})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
