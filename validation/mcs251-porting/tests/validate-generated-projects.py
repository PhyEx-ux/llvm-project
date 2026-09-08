#!/usr/bin/env python3
"""Validate that every v1 generated Makefile parses through GNU make.

`make -n all` evaluates includes, variables, dependencies, and recipes without
attempting to compile the still-unported Keil dialect sources.  Five diverse
projects are retained with their dry-run command text for human inspection.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def c_source_count(record: dict[str, object]) -> int:
    return sum(entry["classification"] == "c" for entry in record["files"])


def select_samples(records: list[dict[str, object]]) -> list[dict[str, object]]:
    predicates = [
        lambda r: r["device"] == "STC32G12K128 Series" and c_source_count(r),
        lambda r: r["device"] == "STC32G144K246 Series" and c_source_count(r),
        lambda r: r["device"] == "STC32G144K246-32Bit Series" and c_source_count(r),
        lambda r: r["include_paths"] and c_source_count(r),
        lambda r: c_source_count(r) > 1,
    ]
    selected: list[dict[str, object]] = []
    seen: set[str] = set()
    for predicate in predicates:
        record = next(
            (candidate for candidate in records if predicate(candidate) and candidate["slug"] not in seen),
            None,
        )
        if record is None:
            raise RuntimeError("cannot select a required diverse project sample")
        selected.append(record)
        seen.add(str(record["slug"]))
    return selected


def run_make(makefile: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["make", "--no-print-directory", "-C", str(makefile.parent), "-n", "all"],
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="uvproj2make JSON report")
    parser.add_argument("generated_root", type=Path, help="generated project root")
    parser.add_argument("--output", required=True, type=Path, help="write validation JSON")
    args = parser.parse_args()

    report = json.loads(args.report.read_text(encoding="utf-8"))
    records = report["project_records"]
    makefiles = sorted(args.generated_root.rglob("Makefile"))
    expected = len(records)
    if len(makefiles) != expected:
        raise RuntimeError(f"expected {expected} Makefiles, found {len(makefiles)}")

    results: dict[str, dict[str, object]] = {}
    failed: list[str] = []
    failure_details: list[dict[str, object]] = []
    for makefile in makefiles:
        completed = run_make(makefile)
        key = makefile.parent.name
        results[key] = {
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
        if completed.returncode:
            failed.append(key)
            failure_details.append(
                {
                    "makefile": f"{key}/Makefile",
                    "returncode": completed.returncode,
                    "stdout": completed.stdout,
                    "stderr": completed.stderr,
                }
            )

    samples = []
    for record in select_samples(records):
        checked = results[str(record["slug"])]
        samples.append(
            {
                "uvproj": record["uvproj"],
                "makefile": f"{record['slug']}/Makefile",
                "device": record["device"],
                "board": record["board"],
                "c_sources": c_source_count(record),
                "include_paths": record["include_paths"],
                "dry_run": checked,
            }
        )
    if len({sample["makefile"] for sample in samples}) != len(samples):
        raise RuntimeError("diverse sample selection produced a duplicate Makefile")

    output = {
        "format": "mcs251-porting-makefile-validation-v1",
        "makefiles_checked": len(makefiles),
        "makefiles_passed": len(makefiles) - len(failed),
        "makefiles_failed": failed,
        "failure_details": failure_details,
        "sample_inspections": samples,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"makefiles_checked={len(makefiles)} passed={len(makefiles) - len(failed)} failed={len(failed)}")
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as exc:
        print(f"validate-generated-projects: error: {exc}", file=sys.stderr)
        sys.exit(2)
