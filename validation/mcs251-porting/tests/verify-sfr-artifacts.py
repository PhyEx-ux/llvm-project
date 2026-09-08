#!/usr/bin/env python3
"""Independently cross-check v1 SFR reports, headers, source identity, and checks.

The verifier treats the converter report as evidence, not authority. It rejects
folded duplicate macros, absent XFR/sbit documentation, and source/report
identity mismatches before accepting compiler self-check evidence elsewhere.

Scope note: this tool verifies report/header/self-check *consistency*, not the
semantics of the source header. A coordinated tamper that simultaneously
clears a demotion record's as6_demoted flag and deletes the matching
registration comment leaves every cross-check and the source SHA-256 intact;
detecting it would require re-classifying the source, which is out of scope
here (the earlier reason-text-matching scheme had the same boundary).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

DIRECT_DEFINE_RES = {
    # 0: compatibility flavor (v1 original); 6: v2 memory-contract flavor.
    # Keyed by the report's sfr_address_space field (absent means 0).
    0: re.compile(
        r"^#define\s+(?P<name>[A-Za-z_]\w*)\s+"
        r"\(\*\(volatile unsigned char \*\)0x(?P<address>[0-9A-F]{2})\)$",
        re.MULTILINE,
    ),
    6: re.compile(
        r"^#define\s+(?P<name>[A-Za-z_]\w*)\s+"
        r"\(\*\(volatile unsigned char __attribute__\(\(address_space\(6\)\)\) \*\)"
        r"0x(?P<address>[0-9A-F]{2})\)$",
        re.MULTILINE,
    ),
}
XFR_COMMENT_RE = re.compile(
    r"^/\* XFR (?P<name>[A-Za-z_]\w*) = 0x(?P<address>[0-9A-F]+) "
    r"\((?P<qualifier>xdata|far)\); not defined in v1\. \*/$",
    re.MULTILINE,
)
SBIT_COMMENT_RE = re.compile(
    r"^/\* sbit (?P<name>[A-Za-z_]\w*) = (?P<byte>[A-Za-z_]\w*)\^(?P<bit>[0-7]); "
    r"not defined in v1\. \*/$",
    re.MULTILINE,
)
# AS6 demotion registration: direct SFRs the backend cannot express in address
# space 6 (constant address 0xFF aborts llc) are rendered as named comments
# instead of macros. The name and address in the comment are contract-bound to
# the report's ignored audit record carrying the structured "as6_demoted":
# true flag set by the converter (tools/sfr-convert.py
# effective_for_address_space). The flag, not the reason text, identifies a
# demotion: ignored reasons quote source excerpts, so a decoy comment inside
# an inactive "#if 0" declaration can carry wording identical to the fixed
# demotion reason.
AS6_DEMOTED_COMMENT_RE = re.compile(
    r"^/\* sfr (?P<name>[A-Za-z_]\w*) = 0x(?P<address>[0-9A-F]{2}); "
    r"not representable in AS6\. \*/$",
    re.MULTILINE,
)
IDENTIFIER_RE = re.compile(r"[A-Za-z_]\w*")
SELF_CHECK_USE_RE = re.compile(r"\bsink \^= (?P<name>[A-Za-z_]\w*);")


class VerificationError(ValueError):
    pass


def uniqueness(entries: list[dict[str, object]], category: str, report_path: Path) -> None:
    names = [str(entry.get("name", "")) for entry in entries]
    duplicates = sorted(name for name, count in Counter(names).items() if count > 1)
    if duplicates:
        raise VerificationError(f"{report_path}: duplicate {category} names: {duplicates}")


def as_direct_mapping(entries: list[dict[str, object]]) -> dict[str, int]:
    return {str(entry["name"]): int(entry["address"]) for entry in entries}


def as_xfr_mapping(entries: list[dict[str, object]]) -> dict[str, tuple[int, str]]:
    return {
        str(entry["name"]): (int(entry["address"]), str(entry["qualifier"])) for entry in entries
    }


def as_sbit_mapping(entries: list[dict[str, object]]) -> dict[str, tuple[str, int]]:
    return {str(entry["name"]): (str(entry["byte"]), int(entry["bit"])) for entry in entries}


def verify_one(report_path: Path, header_path: Path, check_path: Path) -> dict[str, object]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("format") != "mcs251-sfr-convert-v1":
        raise VerificationError(f"{report_path}: unexpected report format")
    address_space = report.get("sfr_address_space", 0)
    if address_space not in DIRECT_DEFINE_RES:
        raise VerificationError(f"{report_path}: unsupported sfr_address_space {address_space!r}")
    direct_define_re = DIRECT_DEFINE_RES[address_space]
    # The macro flavor declared by the report must match the one rendered in
    # the header: an AS0 report must never bless AS6 macros, and vice versa.
    wrong_space = address_space ^ 6 if address_space in (0, 6) else 0
    if wrong_space in DIRECT_DEFINE_RES and DIRECT_DEFINE_RES[wrong_space].search(
        header_path.read_text(encoding="utf-8")
    ):
        raise VerificationError(
            f"{header_path}: contains address-space-{wrong_space} macros "
            f"while the report declares {address_space}"
        )

    # direct_sfr/xfr/sbit are effective mappings: one name must resolve to
    # exactly one macro/comment, so duplicates are rejected. "ignored" is an
    # audit trail, not a mapping: the converter legitimately records several
    # entries under the same placeholder (e.g. two legal "#if 0" registrations
    # are both named "<inactive>", and repeated duplicate-name rejections keep
    # the offending name). Ignored records are still list/counts bound below;
    # they just do not participate in name uniqueness.
    categories = ("direct_sfr", "xfr", "sbit")
    for category in categories:
        entries = report.get(category)
        if not isinstance(entries, list):
            raise VerificationError(f"{report_path}: {category} is not a list")
        if report.get("counts", {}).get(category) != len(entries):
            raise VerificationError(
                f"{report_path}: counts.{category} does not equal its entry list length"
            )
        uniqueness(entries, category, report_path)
    ignored = report.get("ignored")
    if not isinstance(ignored, list):
        raise VerificationError(f"{report_path}: ignored is not a list")
    if report.get("counts", {}).get("ignored") != len(ignored):
        raise VerificationError(f"{report_path}: counts.ignored does not equal its entry list length")
    if not all(isinstance(entry, dict) for entry in ignored):
        raise VerificationError(f"{report_path}: ignored contains a non-object audit record")

    source = Path(str(report.get("source", "")))
    source_hash = report.get("source_sha256")
    if not source.is_file() or not isinstance(source_hash, str):
        raise VerificationError(f"{report_path}: missing verifiable source identity")
    if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
        raise VerificationError(f"{report_path}: source SHA-256 no longer matches report")

    direct = report["direct_sfr"]
    xfr = report["xfr"]
    sbit = report["sbit"]
    # The AS6 backend's direct window tops out at 0xFE (a constant address of
    # 0xFF in address space 6 aborts llc), so an AS6 report must never keep a
    # 0xFF register among its effective direct mappings. AS0 keeps the full
    # legacy 0x80..0xFF window.
    direct_window_max = 0xFE if address_space == 6 else 0xFF
    if not all(0x80 <= int(entry["address"]) <= direct_window_max for entry in direct):
        window = f"0x80..0x{direct_window_max:02X}"
        detail = (
            "; 0xFF is backend-forbidden in AS6 and must be demoted to an "
            "ignored record with a registration comment"
            if address_space == 6
            else ""
        )
        raise VerificationError(f"{report_path}: direct SFR outside {window}{detail}")
    if not all(
        0xFE00 <= int(entry["address"]) <= 0xFFFF
        or 0x7EF000 <= int(entry["address"]) <= 0x7EFFFF
        for entry in xfr
    ):
        raise VerificationError(f"{report_path}: XFR outside documented physical ranges")
    if not all(0 <= int(entry["bit"]) <= 7 and entry["byte"] for entry in sbit):
        raise VerificationError(f"{report_path}: malformed sbit normalization")

    header = header_path.read_text(encoding="utf-8")
    direct_matches = list(direct_define_re.finditer(header))
    xfr_matches = list(XFR_COMMENT_RE.finditer(header))
    sbit_matches = list(SBIT_COMMENT_RE.finditer(header))
    rendered = {match.group("name"): int(match.group("address"), 16) for match in direct_matches}
    rendered_xfr = {
        match.group("name"): (int(match.group("address"), 16), match.group("qualifier"))
        for match in xfr_matches
    }
    rendered_sbit = {
        match.group("name"): (match.group("byte"), int(match.group("bit")))
        for match in sbit_matches
    }
    reported = as_direct_mapping(direct)
    reported_xfr = as_xfr_mapping(xfr)
    reported_sbit = as_sbit_mapping(sbit)
    # Compare list count and mapping: dictionaries alone would fold duplicates.
    if len(direct_matches) != len(rendered) or len(direct_matches) != len(reported):
        raise VerificationError(f"{header_path}: duplicate or missing direct SFR macro")
    if len(xfr_matches) != len(rendered_xfr) or len(xfr_matches) != len(reported_xfr):
        raise VerificationError(f"{header_path}: duplicate or missing XFR comment")
    if len(sbit_matches) != len(rendered_sbit) or len(sbit_matches) != len(reported_sbit):
        raise VerificationError(f"{header_path}: duplicate or missing sbit comment")
    if rendered != reported:
        raise VerificationError(f"{header_path}: direct macro mapping differs from report")
    if rendered_xfr != reported_xfr:
        raise VerificationError(f"{header_path}: XFR comment mapping differs from report")
    if rendered_sbit != reported_sbit:
        raise VerificationError(f"{header_path}: sbit comment mapping differs from report")

    # AS6 0xFF demotion cross-check: the header's "not representable in AS6"
    # registration comments and the report's ignored audit records must agree
    # on both the register name and the address (exactly 0xFF). Demotion
    # identity is the structured "as6_demoted" flag only — never reason text,
    # which quotes source excerpts. This closes the fake-PASS shapes from the
    # 2026-09-08 quick review (deleting the registration comment; keeping a
    # 0xFF register in direct_sfr with a macro) and the recheck regression
    # (a decoy comment inside an inactive declaration impersonating the
    # demotion wording and crashing the verifier on int(None)).
    demoted_matches = list(AS6_DEMOTED_COMMENT_RE.finditer(header))
    rendered_demoted = {
        match.group("name"): int(match.group("address"), 16) for match in demoted_matches
    }
    if len(demoted_matches) != len(rendered_demoted):
        raise VerificationError(f"{header_path}: duplicate AS6 0xFF registration comment")
    demotion_records = [entry for entry in ignored if entry.get("as6_demoted") is True]
    # A record claiming the demotion flag must be well formed before it may
    # build a mapping: a name that is a valid string identifier, and an
    # address that is exactly the integer 0xFF — type-checked, because JSON
    # 255.0 compares equal to 255 under plain equality and would otherwise
    # sail through. Malformed claims are rejected outright; they are never
    # filtered away, and missing fields must fail as a VerificationError,
    # never as a KeyError or TypeError.
    for entry in demotion_records:
        name = entry.get("name")
        if not isinstance(name, str) or not IDENTIFIER_RE.fullmatch(name):
            raise VerificationError(
                f"{report_path}: malformed as6_demoted record: name must be a "
                f"string identifier, got {name!r}"
            )
        if type(entry.get("address")) is not int or entry["address"] != 0xFF:
            raise VerificationError(
                f"{report_path}: malformed as6_demoted record: the demotion "
                f"slot must be the integer address 0xFF, got "
                f"{entry.get('address')!r}"
            )
    reported_demoted = {str(entry["name"]): int(entry["address"]) for entry in demotion_records}
    if len(demotion_records) != len(reported_demoted):
        raise VerificationError(f"{report_path}: duplicate AS6 demotion audit records")
    if address_space == 6:
        if rendered_demoted != reported_demoted:
            raise VerificationError(
                f"{header_path}: AS6 0xFF registration comments differ from the "
                f"report's ignored audit records"
            )
    elif demoted_matches or demotion_records:
        # The converter emits registration comments and flagged demotion
        # audit records only in the AS6 flavor; either one paired with an AS0
        # report is a flavor tamper.
        raise VerificationError(
            f"{header_path}: AS6 0xFF registration artifacts paired with an "
            f"address-space-0 report"
        )

    check_uses = SELF_CHECK_USE_RE.findall(check_path.read_text(encoding="utf-8"))
    if len(check_uses) != len(set(check_uses)) or set(check_uses) != set(reported):
        raise VerificationError(f"{check_path}: does not use every direct SFR exactly once")

    return {
        "report": str(report_path),
        "header": str(header_path),
        "selfcheck": str(check_path),
        "source": str(source),
        "source_sha256": source_hash,
        "sfr_address_space": address_space,
        "counts": report["counts"],
        "raw_declaration_counts": report.get("raw_declaration_counts", {}),
        "rendered_direct_macros": len(direct_matches),
        "rendered_xfr_comments": len(xfr_matches),
        "rendered_sbit_comments": len(sbit_matches),
        "rendered_as6_demoted_comments": len(demoted_matches),
        "selfcheck_direct_uses": len(check_uses),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="validation/mcs251-porting directory")
    parser.add_argument("--output", required=True, type=Path, help="write verification JSON")
    args = parser.parse_args()
    root = args.root.resolve()
    # (report, header, selfcheck) per flavor. The AS6 flavor demotes direct
    # SFRs at 0xFF (backend-forbidden in AS6) to registered comments, so its
    # self-check sources use the reduced effective set; all other register
    # names are shared with the AS0 flavor.
    pairs = [
        ("stc32g-v1-report.json", "stc32g-v1.h", "stc32g-direct-sfr-selfcheck.c"),
        (
            "stc32g144k246-v1-report.json",
            "stc32g144k246-v1.h",
            "stc32g144k246-direct-sfr-selfcheck.c",
        ),
        ("stc32g-as6-report.json", "stc32g-as6.h", "stc32g-direct-sfr-selfcheck-as6.c"),
        (
            "stc32g144k246-as6-report.json",
            "stc32g144k246-as6.h",
            "stc32g144k246-direct-sfr-selfcheck-as6.c",
        ),
    ]
    results = [
        verify_one(root / "reports" / report, root / "generated" / header, root / "tests" / check)
        for report, header, check in pairs
    ]
    # v3: AS6 direct window tightened to 0x80..0xFE and the 0xFF demotion
    # registration comments are cross-checked against the report's ignored
    # audit records (name and address).
    output = {"format": "mcs251-porting-sfr-artifact-validation-v3", "passed": True, "headers": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"headers_checked={len(results)} passed={len(results)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, VerificationError, ValueError, KeyError, TypeError) as exc:
        print(f"verify-sfr-artifacts: error: {exc}", file=sys.stderr)
        sys.exit(2)
