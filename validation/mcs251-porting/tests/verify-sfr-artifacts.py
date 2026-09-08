#!/usr/bin/env python3
"""Independently cross-check v1 SFR reports, headers, source identity, and checks.

The verifier treats the converter report as evidence, not authority. It rejects
folded duplicate macros, absent XFR/sbit documentation, and source/report
identity mismatches before accepting compiler self-check evidence elsewhere.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

DIRECT_DEFINE_RE = re.compile(
    r"^#define\s+(?P<name>[A-Za-z_]\w*)\s+"
    r"\(\*\(volatile unsigned char \*\)0x(?P<address>[0-9A-F]{2})\)$",
    re.MULTILINE,
)
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
    if not all(0x80 <= int(entry["address"]) <= 0xFF for entry in direct):
        raise VerificationError(f"{report_path}: direct SFR outside 0x80..0xFF")
    if not all(
        0xFE00 <= int(entry["address"]) <= 0xFFFF
        or 0x7EF000 <= int(entry["address"]) <= 0x7EFFFF
        for entry in xfr
    ):
        raise VerificationError(f"{report_path}: XFR outside documented physical ranges")
    if not all(0 <= int(entry["bit"]) <= 7 and entry["byte"] for entry in sbit):
        raise VerificationError(f"{report_path}: malformed sbit normalization")

    header = header_path.read_text(encoding="utf-8")
    direct_matches = list(DIRECT_DEFINE_RE.finditer(header))
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

    check_uses = SELF_CHECK_USE_RE.findall(check_path.read_text(encoding="utf-8"))
    if len(check_uses) != len(set(check_uses)) or set(check_uses) != set(reported):
        raise VerificationError(f"{check_path}: does not use every direct SFR exactly once")

    return {
        "report": str(report_path),
        "header": str(header_path),
        "selfcheck": str(check_path),
        "source": str(source),
        "source_sha256": source_hash,
        "counts": report["counts"],
        "raw_declaration_counts": report.get("raw_declaration_counts", {}),
        "rendered_direct_macros": len(direct_matches),
        "rendered_xfr_comments": len(xfr_matches),
        "rendered_sbit_comments": len(sbit_matches),
        "selfcheck_direct_uses": len(check_uses),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="validation/mcs251-porting directory")
    parser.add_argument("--output", required=True, type=Path, help="write verification JSON")
    args = parser.parse_args()
    root = args.root.resolve()
    pairs = [
        ("stc32g-v1-report.json", "stc32g-v1.h", "stc32g-direct-sfr-selfcheck.c"),
        (
            "stc32g144k246-v1-report.json",
            "stc32g144k246-v1.h",
            "stc32g144k246-direct-sfr-selfcheck.c",
        ),
    ]
    results = [
        verify_one(root / "reports" / report, root / "generated" / header, root / "tests" / check)
        for report, header, check in pairs
    ]
    output = {"format": "mcs251-porting-sfr-artifact-validation-v2", "passed": True, "headers": results}
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
