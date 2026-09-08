#!/usr/bin/env python3
"""Assemble v1 acceptance only after independent, input-bound checks pass."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

LAYOUT_FLAGS = (
    "--area-start=HOME=0xff0000",
    "--area-start=VECS=0xff0003",
    "--area-start=BOOT=0xff0100",
    "--area-start=CSEG=$(CSEG_BASE)",
    "--area-start=XINIT=$(XINIT_BASE)",
)
RAW_SOURCE_ASSIGNMENT_RE = re.compile(r"^C_SOURCE_RAW_(\d{3}) := (.+)$", re.MULTILINE)
# Characters the converter refuses to render into a generated build rule
# (mirrors UNSAFE_BUILD_PATH_RE in tools/uvproj2make.py). An enabled C entry
# whose resolved_path contains any of them is downgraded to "registered but
# not built": no C_SOURCE/C_SOURCE_RAW variable and no compile recipe is
# emitted, so the final gate must not expect one in the raw-source sequence.
UNSAFE_BUILD_PATH_RE = re.compile(r"[$#`'\"\\\r\n\t;:|=*?\[\]]")


def load(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def require_passed(report: dict[str, object], name: str) -> None:
    if report.get("passed") is not True:
        raise RuntimeError(f"{name} report is not an explicit pass")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def recipe_block(text: str, target_prefix: str) -> str:
    marker = text.find(target_prefix)
    if marker < 0:
        raise RuntimeError(f"missing rule {target_prefix}")
    end = text.find("\n\n", marker)
    return text[marker:] if end < 0 else text[marker:end]


def buildable_c_entries(record: dict[str, object]) -> list[dict[str, object]]:
    """Enabled C entries that the converter must have given a build rule.

    Mirrors Project.buildable_c_files in the converter: enabled C files that
    resolve to a corpus path free of Make/shell-unrepresentable characters.
    Entries downgraded to "registered but not built" (unsafe path, e.g.
    ``main;>semaphore;.c``) are excluded so their absence from the
    raw-source assignment sequence is not mistaken for a missing or
    tampered rule. Missing, extra, reordered, and tampered rules for the
    genuinely buildable files are still rejected by the caller's exact
    sequence comparison.
    """
    return [
        entry for entry in record["files"]
        if entry["classification"] == "c"
        and entry.get("include_in_build", True)
        and entry.get("resolved_path")
        and not UNSAFE_BUILD_PATH_RE.search(entry["resolved_path"])
    ]


def verify_generated_makefiles(root: Path, uvproj: dict[str, object]) -> dict[str, int]:
    records = uvproj.get("project_records")
    expected_hashes = uvproj.get("generated_makefile_sha256")
    if not isinstance(records, list) or not isinstance(expected_hashes, dict):
        raise RuntimeError("UVPROJ report lacks project records or Makefile identity hashes")
    generated_root = root / "generated" / "projects"
    makefiles = sorted(generated_root.rglob("Makefile"))
    expected_paths = {generated_root / str(record["slug"]) / "Makefile" for record in records}
    if set(makefiles) != expected_paths:
        raise RuntimeError("generated Makefile set does not exactly match this UVPROJ report")
    c_projects = 0
    build_rules = 0
    for record in records:
        slug = str(record["slug"])
        makefile = generated_root / slug / "Makefile"
        if sha256(makefile) != expected_hashes.get(slug):
            raise RuntimeError(f"{makefile}: content does not match this generation report")
        text = makefile.read_text(encoding="utf-8")
        buildable = buildable_c_entries(record)
        # The raw-path assignments must be exactly the buildable sequence, in
        # record order: no missing, extra, reordered, or excluded file rules.
        actual_raw = [match.group(2) for match in RAW_SOURCE_ASSIGNMENT_RE.finditer(text)]
        expected_raw = [f"$(PROJECT_DIR)/{entry['resolved_path']}" for entry in buildable]
        if actual_raw != expected_raw:
            raise RuntimeError(f"{makefile}: build rule set does not match project record")
        if "refusing to clean BUILD outside this generated project" not in text:
            raise RuntimeError(f"{makefile}: clean recipe lacks its BUILD boundary guard")
        if not buildable:
            continue
        c_projects += 1
        for index in range(len(buildable)):
            compile_block = recipe_block(text, f"$(BUILD)/c{index:03d}.ll:")
            if "$(CLANG)" not in compile_block or f'"$(C_SOURCE_RAW_{index:03d})"' not in compile_block:
                raise RuntimeError(f"{makefile}: c{index:03d} compile recipe does not quote its raw source path")
            block = recipe_block(text, f"$(BUILD)/c{index:03d}.o:")
            if "$(LLC)" not in block or "-mcs251-object-format=elf" not in block:
                raise RuntimeError(f"{makefile}: c{index:03d} lacks an ELF llc recipe")
            build_rules += 1
        link = recipe_block(text, "$(BUILD)/firmware.elf:")
        for required in ("$(LLD) -flavor mcs251", "--edata-end $(EDATA_END)", *LAYOUT_FLAGS):
            if required not in link:
                raise RuntimeError(f"{makefile}: link recipe missing {required}")
        hex_rule = recipe_block(text, "$(BUILD)/firmware.hex:")
        if "$(OBJCOPY) -O ihex \"$<\" \"$@\"" not in hex_rule:
            raise RuntimeError(f"{makefile}: Intel HEX rule is not linked-ELF based")
    return {"makefiles": len(makefiles), "c_projects": c_projects, "build_c_rules": build_rules}


DEFAULT_SELFCHECK_STEMS = (
    "stc32g-direct-sfr-selfcheck",
    "stc32g144k246-direct-sfr-selfcheck",
    # AS6 flavors compile the same register uses through address-space-6
    # macros; both flavors assemble under the default layout, while only
    # AS6 lowers to direct SFR moves under the v2 memory contract.
    "stc32g-direct-sfr-selfcheck-as6",
    "stc32g144k246-direct-sfr-selfcheck-as6",
)


def compile_sfr_selfchecks(
    root: Path,
    work: Path,
    clang: str,
    llc: str,
    stems: tuple[str, ...] = DEFAULT_SELFCHECK_STEMS,
) -> list[dict[str, object]]:
    results = []
    for stem in stems:
        source = root / "tests" / f"{stem}.c"
        ll = work / f"{stem}.ll"
        assembly = work / f"{stem}.s"
        commands = [
            [clang, "--target=mcs251-unknown-none", "-std=c11", "-Wall", "-Wextra", "-Werror", "-S", "-emit-llvm", str(source), "-o", str(ll)],
            [llc, "-mtriple=mcs251-unknown-none", "-verify-machineinstrs", str(ll), "-o", str(assembly)],
        ]
        records = []
        for command in commands:
            completed = subprocess.run(command, text=True, encoding="utf-8", capture_output=True, check=False)
            records.append({"command": command, "returncode": completed.returncode})
            if completed.returncode:
                raise RuntimeError(f"{stem}: compiler self-check failed: {completed.stderr.strip()}")
        if not ll.is_file() or not assembly.is_file() or not ll.read_bytes() or not assembly.read_bytes():
            raise RuntimeError(f"{stem}: compiler self-check produced no usable output")
        results.append({"name": stem, "ir_sha256": sha256(ll), "assembly_sha256": sha256(assembly), "steps": records})
    return results


def elf_text_and_nop_count(path: Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or len(data) < 52 or data[4] != 1 or data[5] not in {1, 2}:
        raise RuntimeError(f"{path}: not an ELF32 object")
    endian = ">" if data[5] == 2 else "<"
    shoff = struct.unpack_from(endian + "I", data, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from(endian + "HHH", data, 46)
    if shentsize < 40 or not shnum or shstrndx >= shnum:
        raise RuntimeError(f"{path}: malformed ELF section table")
    def section(index: int) -> tuple[int, int, int]:
        start = shoff + index * shentsize
        if start + 40 > len(data):
            raise RuntimeError(f"{path}: truncated ELF section table")
        name, _kind, _flags, _addr, offset, size = struct.unpack_from(endian + "IIIIII", data, start)
        return name, offset, size
    _name, strings_offset, strings_size = section(shstrndx)
    strings = data[strings_offset:strings_offset + strings_size]
    for index in range(shnum):
        name_offset, offset, size = section(index)
        name_end = strings.find(b"\0", name_offset)
        if name_end >= 0 and strings[name_offset:name_end] == b".text":
            text = data[offset:offset + size]
            return text, text.count(b"\x00")
    raise RuntimeError(f"{path}: no .text section")


def verify_real_elf_nop(root: Path, work: Path, yaml2obj: str) -> dict[str, object]:
    source = root / "runtime" / "elf-nop.yaml"
    obj = work / "elf-nop.o"
    completed = subprocess.run([yaml2obj, str(source), "-o", str(obj)], text=True, encoding="utf-8", capture_output=True, check=False)
    if completed.returncode:
        raise RuntimeError(f"yaml2obj ELF NOP generation failed: {completed.stderr.strip()}")
    text, nop_count = elf_text_and_nop_count(obj)
    if text != b"\x00\xAA" or nop_count != 1:
        raise RuntimeError(f"ELF NOP helper must be exactly 00 AA with one NOP, got {text.hex()} ({nop_count})")
    return {"object_sha256": sha256(obj), "text_bytes": text.hex(" ").upper(), "nop_opcode_count": nop_count, "yaml2obj_returncode": completed.returncode}


def require_step_records(report: dict[str, object], name: str) -> int:
    """Strongly validate every step record; a malformed record is a failure.

    Filtering out non-object elements and then testing ``any()`` accepts an
    all-malformed list such as ``steps: [null]``; this loop rejects each
    non-dict or field-incomplete record instead of skipping it.
    """
    steps = report.get("steps")
    if not isinstance(steps, list) or not steps:
        raise RuntimeError(f"{name}: missing step records")
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise RuntimeError(f"{name}: step record {index} is not an object")
        if not isinstance(step.get("step"), str) or not step.get("step"):
            raise RuntimeError(f"{name}: step record {index} lacks a step name")
        command = step.get("command")
        if not isinstance(command, list) or not command or not all(isinstance(word, str) for word in command):
            raise RuntimeError(f"{name}: step record {index} lacks a command vector")
        returncode = step.get("returncode")
        if isinstance(returncode, bool) or not isinstance(returncode, int) or returncode != 0:
            raise RuntimeError(f"{name}: step record {index} is not a successful step")
    return len(steps)


def smoke_status(report: dict[str, object], name: str) -> dict[str, object]:
    require_passed(report, name)
    step_count = require_step_records(report, name)
    if report.get("object_magic") != "7f454c46" or report.get("elf_magic") != "7f454c46" or report.get("hex_starts_with_colon") is not True:
        raise RuntimeError(f"{name}: ELF/HEX output identity check failed")
    return {"steps": step_count, "object_magic": report["object_magic"], "elf_magic": report["elf_magic"]}


def dry_run_coverage(
    report: dict[str, object], uvproj: dict[str, object], coverage: dict[str, int]
) -> dict[str, int]:
    """Bind the dry-run report to the Makefile set of this exact generation.

    ``checked == passed`` alone accepts a zero-coverage ``{checked: 0,
    passed: 0}`` report and stale reports from an older generation. The count
    must equal both the records of the UVPROJ report and the Makefiles that
    actually exist under generated/ (already set-compared by
    verify_generated_makefiles), and every sample inspection must reference a
    current Makefile with a successful dry run.
    """
    if report.get("format") != "mcs251-porting-makefile-validation-v1":
        raise RuntimeError("generated Makefile dry-run report has an unexpected format")
    records = uvproj.get("project_records")
    expected = coverage.get("makefiles")
    if not isinstance(records, list) or len(records) != expected or not isinstance(expected, int) or expected <= 0:
        raise RuntimeError("cannot bind dry-run coverage to a non-empty generated Makefile set")
    checked = report.get("makefiles_checked")
    if isinstance(checked, bool) or not isinstance(checked, int) or checked != expected:
        raise RuntimeError(f"dry-run report covers {checked!r} Makefiles, expected exactly {expected} for this generation")
    passed = report.get("makefiles_passed")
    if isinstance(passed, bool) or not isinstance(passed, int) or passed != expected:
        raise RuntimeError("dry-run report contains Makefiles that did not pass")
    if report.get("makefiles_failed") != []:
        raise RuntimeError("generated Makefile dry-run report contains failures")
    slugs = {str(record["slug"]) for record in records}
    samples = report.get("sample_inspections")
    if not isinstance(samples, list) or not samples:
        raise RuntimeError("dry-run report lacks sample inspections")
    for sample in samples:
        if not isinstance(sample, dict):
            raise RuntimeError("dry-run sample inspection is not an object")
        makefile = sample.get("makefile")
        if not isinstance(makefile, str) or "/Makefile" not in makefile or makefile.split("/Makefile")[0] not in slugs:
            raise RuntimeError(f"dry-run sample inspection references unknown Makefile {makefile!r}")
        dry = sample.get("dry_run")
        if not isinstance(dry, dict) or dry.get("returncode") != 0:
            raise RuntimeError(f"dry-run sample inspection of {makefile} is not a success")
    return {"checked": checked, "passed": passed, "failed": 0, "samples": len(samples)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="validation/mcs251-porting directory")
    parser.add_argument("evidence", type=Path, help="WSL evidence directory")
    parser.add_argument("--clang", default="/home/liu/build-clang/bin/clang")
    parser.add_argument("--llc", default="/home/liu/mcs251-demo-test/bin-frozen/llc")
    parser.add_argument("--yaml2obj", default="/home/liu/mcs251-demo-test/bin-frozen/yaml2obj")
    args = parser.parse_args()
    root = args.root.resolve()
    reports = root / "reports"
    evidence = args.evidence.resolve()

    sfr = load(reports / "sfr-artifact-validation.json")
    intrinsics = load(reports / "intrinsics-audit.json")
    uvproj = load(reports / "uvproj-summary.json")
    makefiles = load(reports / "generated-makefile-validation.json")
    smoke = load(reports / "toolchain-smoke.json")
    sample = load(reports / "minimal-direct-sfr-validation.json")
    require_passed(sfr, "SFR artifact")
    if uvproj.get("parse_failures") != []:
        raise RuntimeError("UVPROJ parse acceptance failed")
    # Every input report is bound and rejected before any toolchain runs, so
    # tampered sub-reports cannot hide behind an unrelated compiler failure.
    makefile_coverage = verify_generated_makefiles(root, uvproj)
    dry_run = dry_run_coverage(makefiles, uvproj, makefile_coverage)
    lexical = intrinsics.get("lexical_call_candidates", {})
    raw = intrinsics.get("raw_text_call_matches", {})
    if raw.get("counts") != {"_nop_": 608} or lexical.get("counts") != {"_nop_": 585} or lexical.get("file_counts") != {"_nop_": 79}:
        raise RuntimeError("intrinsic audit counting bases do not match the pinned 608/585/79 acceptance")
    if lexical.get("only_nop_called") is not True:
        raise RuntimeError("lexical intrinsic audit includes unsupported intrinsic calls")
    smoke_checked = smoke_status(smoke, "ELF smoke")
    sample_checked = smoke_status(sample, "minimal QEMU sample")
    if sample.get("uart_actual") != "PORTING-MINIMAL-PASS\n":
        raise RuntimeError("minimal QEMU sample UART sentinel failed")

    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mcs251-final-", dir=evidence) as temporary:
        work = Path(temporary)
        selfchecks = compile_sfr_selfchecks(root, work, args.clang, args.llc)
        nop = verify_real_elf_nop(root, work, args.yaml2obj)

    output = {
        "format": "mcs251-porting-v1-final-validation-v4",
        "passed": True,
        "input_binding": {
            "sfr_reports_verified": [header["source_sha256"] for header in sfr["headers"]],
            "uvproj_records": uvproj["projects"],
            "generated_makefiles": makefile_coverage["makefiles"],
        },
        "sfr": {"artifact_report_format": sfr["format"], "compiler_selfchecks": selfchecks},
        "intrinsics": {
            "raw_text_matches": raw,
            "lexical_call_candidates": lexical,
            "preprocessed_call_candidates": intrinsics.get("preprocessed_call_candidates"),
        },
        "uvproj": {
            "projects": uvproj["projects"],
            "listed_c_source_entries": uvproj.get("listed_c_source_entries"),
            "enabled_c_source_entries": uvproj.get("enabled_c_source_entries"),
            "build_rule_c_source_entries": uvproj.get("build_rule_c_source_entries"),
            "excluded_c_source_entries": uvproj.get("excluded_c_source_entries"),
            "makefile_coverage": makefile_coverage,
            "dry_run_checked": dry_run["checked"],
            "dry_run_binding": dry_run,
        },
        "elf_production": {"smoke": smoke_checked, "verified_nop_helper": nop, "minimal_qemu": sample_checked},
        "remaining_v1_limits": [
            "Keil sbit/bit/xdata/far/_at_/interrupt declarations and Keil assembly remain outside source migration v1.",
            "ELF _nop_() uses an audited NOP; ERET helper and is not cycle-identical to Keil inline _nop_().",
        ],
    }
    destination = reports / "final-validation.json"
    destination.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for name in ("sfr-artifact-validation.json", "intrinsics-audit.json", "uvproj-summary.json", "uvproj-projects.tsv", "generated-makefile-validation.json", "toolchain-smoke.json", "minimal-direct-sfr-validation.json", "final-validation.json"):
        shutil.copyfile(reports / name, evidence / name)
    print("final-validation: PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, KeyError, ValueError, TypeError, struct.error) as exc:
        print(f"assemble-final-validation: error: {exc}", file=sys.stderr)
        sys.exit(1)
