#!/usr/bin/env python3
"""Extract legacy Keil .uvproj metadata and emit MCS251 ELF Makefiles.

This v1 harness migrates project descriptions, not Keil C syntax.  It retains
all listed files for audit and emits the current production path:
clang -> llc (-mcs251-object-format=elf) -> mcs251-lld -> llvm-objcopy.

The XML fields become Makefile and shell input.  Project metadata is therefore
validated before rendering rather than treated as trusted command text.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import posixpath
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

DEVICE_TO_BOARD = {
    "STC32G12K128 Series": "stc32g12k128",
    "STC32G144K246 Series": "stc32g144k246",
    "STC32G144K246-32Bit Series": "stc32g144k246",
}
C_EXTENSIONS = {".c"}
ASM_EXTENSIONS = {".asm", ".a51", ".s"}
# A generated recipe always single-quotes untrusted include/define arguments.
# Reject the characters that can alter Make parsing, escape that quote boundary,
# or produce a second shell argument. Spaces remain supported for include paths.
UNSAFE_METADATA_RE = re.compile(r"[$#`'\"\r\n]")
SAFE_DEFINE_RE = re.compile(r"[A-Za-z_]\w*(?:=[A-Za-z0-9_./:+,\-]+)?$")
# Characters that change Make variable-assignment parsing, break either shell
# quoting layer, act as Make/shell wildcards, or alter rule syntax once a path
# variable is expanded into a prerequisite list. GNU make reads an expanded
# ';' as an inline recipe separator (real command execution), ':' as the
# target/prerequisite delimiter, '|' as the order-only prerequisite boundary,
# TAB as a prerequisite word separator, and '=' as a target-specific variable
# assignment (which silently drops the prerequisite). A listed source file
# containing one stays audit-only instead of being built.
UNSAFE_BUILD_PATH_RE = re.compile(r"[$#`'\"\\\r\n\t;:|=*?\[\]]")
# The project directory is interpolated into unquoted Make assignments and
# single-quoted shell arguments, and expanded inside prerequisite variables;
# it must survive both the Make rule syntax and the shell quoting layers.
UNSAFE_PROJECT_DIR_RE = re.compile(r"[$#`'\"\\\r\n\t;:|=*?\[\]]")
XML_DECLARATION_RE = re.compile(r"^\s*<\?xml[^>]*\?>", re.DOTALL)


class ProjectError(ValueError):
    """A .uvproj that cannot be faithfully represented by v1."""


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def find_child(element: ET.Element, name: str) -> ET.Element | None:
    """Find one direct child independent of an optional XML namespace."""
    return next((child for child in element if local_name(child.tag) == name), None)


def find_text(element: ET.Element, path: str) -> str:
    """Find an XML path by local names, supporting default XML namespaces."""
    current: ET.Element | None = element
    for component in (item for item in path.split("/") if item and item != "."):
        if current is None:
            break
        current = find_child(current, component)
    return (current.text or "").strip() if current is not None else ""


def split_keil_list(value: str) -> list[str]:
    """Split Keil semicolon lists, preserving spaces within one element."""
    return [item.strip() for item in value.replace("\r", "").replace("\n", ";").split(";") if item.strip()]


def split_defines(value: str) -> list[str]:
    result: list[str] = []
    for part in split_keil_list(value):
        result.extend(item.strip() for item in part.split(",") if item.strip())
    return result


def posix_keil_path(value: str) -> str:
    value = value.strip().strip('"').replace("\\", "/")
    normalized = posixpath.normpath(value)
    return "." if normalized == "." else normalized


def reject_unsafe_metadata(value: str, field: str) -> None:
    """Reject metadata that cannot be represented as one safe Make/shell arg."""
    if not value:
        raise ProjectError(f"empty {field} is not representable")
    match = UNSAFE_METADATA_RE.search(value)
    if match:
        raise ProjectError(
            f"unsafe {field} {value!r}: character {match.group()!r} is not supported by v1"
        )


def validate_include_path(value: str) -> str:
    normalized = posix_keil_path(value)
    reject_unsafe_metadata(normalized, "IncludePath")
    return normalized


def validate_define(value: str) -> str:
    reject_unsafe_metadata(value, "Define")
    if any(char.isspace() for char in value) or not SAFE_DEFINE_RE.fullmatch(value):
        raise ProjectError(
            f"unsafe Define {value!r}: v1 accepts NAME or NAME=unquoted-safe-value only"
        )
    return value


def make_path_escape(value: str) -> str:
    """Escape a validated pathname only for Make prerequisite parsing.

    This deliberately is not used in PROJECT_DIR or shell arguments: a Make
    escaped space (``\\ ``) is a literal backslash when later placed inside
    shell quotes.  The raw path and Make-prerequisite representations are kept
    separate.
    """
    return value.replace(" ", "\\ ")


def shell_single_quoted(value: str) -> str:
    """Render a validated value as exactly one POSIX shell argument."""
    # Caller validation rejects single quotes and Make/shell metacharacters.
    return f"'{value}'"


def comment_safe(value: str) -> str:
    """Keep untrusted XML text from breaking out of a generated comment line."""
    return "".join(char if char.isprintable() else " " for char in value.replace("\r", " ").replace("\n", " "))


def stable_slug(relative_uvproj: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9]+", "-", PurePosixPath(relative_uvproj).stem).strip("-").lower()
    digest = hashlib.sha1(relative_uvproj.encode("utf-8")).hexdigest()[:12]
    return f"{stem or 'project'}-{digest}"


def classify_extension(path: str) -> str:
    extension = PurePosixPath(path).suffix.lower()
    if extension in C_EXTENSIONS:
        return "c"
    if extension in ASM_EXTENSIONS:
        return "asm"
    return "other"


def include_in_build(element: ET.Element) -> bool:
    # Keil places the setting in FileOption/CommonProperty in real projects;
    # accept the direct form too for compact test fixtures.
    value = find_text(element, "FileOption/CommonProperty/IncludeInBuild") or find_text(
        element, "FileOption/IncludeInBuild"
    )
    return value.casefold() not in {"0", "false", "no"}


@dataclass
class FileEntry:
    name: str
    # Original .uvproj spelling; it remains immutable audit data.
    path: str
    # Corpus spelling used for WSL Make prerequisites if the source exists.
    resolved_path: str | None
    file_type: str
    extension: str
    classification: str
    # Keil's per-file build selection. All entries stay in reports/comments.
    include_in_build: bool

    def build_path_unsafe_reason(self) -> str | None:
        """Why this entry cannot receive a generated build rule, if at all."""
        if self.classification != "c":
            return None
        if self.resolved_path is None:
            return "source not found in corpus"
        match = UNSAFE_BUILD_PATH_RE.search(self.resolved_path)
        if match:
            return f"path character {match.group()!r} is not representable by v1"
        return None


@dataclass
class Project:
    uvproj: str
    uvproj_sha256: str
    slug: str
    target_name: str
    device: str
    board: str
    include_paths: list[str]
    defines: list[str]
    files: list[FileEntry]

    @property
    def c_files(self) -> list[FileEntry]:
        """All listed C files, including Keil-excluded audit entries."""
        return [entry for entry in self.files if entry.classification == "c"]

    @property
    def enabled_c_files(self) -> list[FileEntry]:
        return [entry for entry in self.c_files if entry.include_in_build]

    @property
    def buildable_c_files(self) -> list[FileEntry]:
        """Enabled C files that also resolve to a representable corpus path."""
        return [entry for entry in self.enabled_c_files if entry.build_path_unsafe_reason() is None]

    @property
    def asm_files(self) -> list[FileEntry]:
        return [entry for entry in self.files if entry.classification == "asm"]

    @property
    def source_candidates(self) -> list[FileEntry]:
        return [entry for entry in self.files if entry.classification in {"c", "asm"}]

    @property
    def enabled_source_candidates(self) -> list[FileEntry]:
        return [entry for entry in self.source_candidates if entry.include_in_build]


def resolve_case_preserving_path(project_directory: Path, recorded_path: str) -> str | None:
    """Find a listed file case-insensitively and return its actual spelling.

    Keil projects were generally authored on a case-insensitive filesystem, but
    v1 runs GNU make under WSL. Preserve the original spelling in reports, and
    use the actual corpus spelling only in generated prerequisites.
    """
    candidate = project_directory
    try:
        for component in PurePosixPath(posix_keil_path(recorded_path)).parts:
            if component in {"", "."}:
                continue
            if component == "..":
                candidate = candidate.parent
                continue
            exact = candidate / component
            if exact.exists():
                candidate = exact
                continue
            matches = [child for child in candidate.iterdir() if child.name.casefold() == component.casefold()]
            if len(matches) != 1:
                return None
            candidate = matches[0]
        if not candidate.is_file():
            return None
        return os.path.relpath(candidate, project_directory).replace(os.sep, "/")
    except (OSError, ValueError):
        return None


def parse_xml_document(path: Path) -> ET.Element:
    """Parse a Keil .uvproj as UTF-8 (with or without BOM) or GB18030/GB2312.

    Expat alone rejects a ``gb2312`` XML declaration, so the bytes are decoded
    here and the declaration is removed before ``fromstring`` (which refuses
    encoding declarations inside decoded text).
    """
    data = path.read_bytes()
    failure: Exception | None = None
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            text = data.decode(encoding)
        except (UnicodeDecodeError, LookupError) as exc:
            failure = exc
            continue
        try:
            return ET.fromstring(XML_DECLARATION_RE.sub("", text, count=1))
        except ET.ParseError as exc:
            failure = exc
    raise ProjectError(
        f"{path}: cannot parse XML as UTF-8 or GB2312/GB18030 (namespaced XML is supported): {failure}"
    )


def parse_project(path: Path, corpus_root: Path) -> Project:
    root = parse_xml_document(path)

    targets_parent = find_child(root, "Targets")
    targets = [] if targets_parent is None else [
        item for item in targets_parent if local_name(item.tag) == "Target"
    ]
    if len(targets) != 1:
        raise ProjectError(f"expected exactly one build Target, found {len(targets)}")
    target = targets[0]
    device = find_text(target, "TargetOption/TargetCommonOption/Device")
    if device not in DEVICE_TO_BOARD:
        raise ProjectError(f"unsupported or missing Device {device!r}")

    include_paths: list[str] = []
    for raw in [
        find_text(target, "TargetOption/TargetCommonOption/IncludePath"),
        find_text(target, "TargetOption/Target251/C251/VariousControls/IncludePath"),
    ]:
        for item in split_keil_list(raw):
            normalized = validate_include_path(item)
            if normalized not in include_paths:
                include_paths.append(normalized)

    defines: list[str] = []
    for item in split_defines(find_text(target, "TargetOption/Target251/C251/VariousControls/Define")):
        normalized = validate_define(item)
        if normalized not in defines:
            defines.append(normalized)

    files: list[FileEntry] = []
    for element in target.iter():
        if local_name(element.tag) != "File":
            continue
        name = find_text(element, "FileName")
        raw_path = find_text(element, "FilePath") or name
        if not raw_path:
            continue
        normalized = posix_keil_path(raw_path)
        files.append(
            FileEntry(
                name=name,
                path=normalized,
                resolved_path=resolve_case_preserving_path(path.parent, normalized),
                file_type=find_text(element, "FileType"),
                extension=PurePosixPath(normalized).suffix.lower(),
                classification=classify_extension(normalized),
                include_in_build=include_in_build(element),
            )
        )

    relative = path.relative_to(corpus_root).as_posix()
    project_dir = str(PurePosixPath(relative).parent)
    unsafe = UNSAFE_PROJECT_DIR_RE.search(project_dir)
    if unsafe:
        raise ProjectError(
            f"{relative}: corpus directory {project_dir!r} contains unsupported "
            f"character {unsafe.group()!r}; rename it or migrate this project manually"
        )
    return Project(
        uvproj=relative,
        uvproj_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        slug=stable_slug(relative),
        target_name=find_text(target, "TargetName"),
        device=device,
        board=DEVICE_TO_BOARD[device],
        include_paths=include_paths,
        defines=defines,
        files=files,
    )


def clean_recipe() -> list[str]:
    return [
        "clean:",
        "\t@case \"$(abspath $(BUILD))\" in \"$(abspath $(HERE))\"/*) rm -rf -- \"$(BUILD)\" ;; *) \\",
        "\t  printf '%s\\n' 'refusing to clean BUILD outside this generated project: $(BUILD)' >&2; exit 2 ;; esac",
        "",
    ]


def render_makefile(project: Project) -> str:
    project_dir = PurePosixPath(project.uvproj).parent
    lines = [
        "# Generated by validation/mcs251-porting/tools/uvproj2make.py; do not edit.",
        f"# Keil project: {comment_safe(project.uvproj)}",
        f"# Target: {comment_safe(project.target_name or '<unnamed>')}",
        f"# Device: {project.device}",
        "# v1 scope: metadata/Makefile generation only. Keil sfr/sbit/xdata/",
        "# interrupt/bit, project assembly and source translation remain separate work.",
        "",
        ".DEFAULT_GOAL := all",
        ".NOTPARALLEL:",
        "HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))",
        "PORTING_ROOT ?= $(abspath $(HERE)/../../..)",
        "CORPUS_ROOT ?= /home/liu/stcex/src",
        "BOARD_ROOT ?= $(PORTING_ROOT)/../mcs251-demo-modern/boards",
        # Keep raw spaces for shell arguments and an independent Make-escaped
        # representation for prerequisite expansion.
        f"PROJECT_DIR := $(CORPUS_ROOT)/{project_dir}",
        f"PROJECT_DIR_MAKE := $(CORPUS_ROOT)/{make_path_escape(str(project_dir))}",
        f"BOARD := {project.board}",
        "include $(BOARD_ROOT)/$(BOARD).mk",
        "",
        "# Tools default to hash-pinned frozen binaries; the s1 build tree is",
        "# rebuilt in place and is not a stable identity. See",
        "# /home/liu/mcs251-demo-test/bin-frozen/porting-v1-tools.identity.txt.",
        "CLANG ?= /home/liu/build-clang/bin/clang",
        "LLC ?= /home/liu/mcs251-demo-test/bin-frozen/llc",
        "LLD ?= /home/liu/build-mcs251-lld/bin/lld",
        "OBJCOPY ?= /home/liu/mcs251-demo-test/bin-frozen/llvm-objcopy",
        "YAML2OBJ ?= /home/liu/mcs251-demo-test/bin-frozen/yaml2obj",
        "ELF_NOP_YAML ?= $(PORTING_ROOT)/runtime/elf-nop.yaml",
        "BUILD ?= $(HERE)/build",
        "",
        "CFLAGS ?= --target=mcs251-unknown-none -std=c11 -O2 -Wall -Wextra",
        "CPPFLAGS += -I$(PORTING_ROOT)/include",
        "# Current ELF streamer needs this real NOP/ERET helper instead of text inline asm.",
        "CPPFLAGS += -DMCS251_PORTING_ELF_NOP_HELPER",
    ]
    for include_path in project.include_paths:
        include_argument = "-I$(PROJECT_DIR)" if include_path == "." else f"-I$(PROJECT_DIR)/{include_path}"
        lines.append(f"CPPFLAGS += {shell_single_quoted(include_argument)}")
    for define in project.defines:
        lines.append(f"CPPFLAGS += {shell_single_quoted('-D' + define)}")

    lines.extend(["", "# Every Keil File entry is retained for migration audit."])
    for index, entry in enumerate(project.files):
        audit = (
            f"# FILE[{index:03d}] type={comment_safe(entry.file_type or '?')} class={entry.classification} "
            f"include_in_build={int(entry.include_in_build)}: {comment_safe(entry.path)}"
        )
        if entry.classification in {"c", "asm"} and entry.resolved_path is None:
            audit += " [not found in corpus; retained for migration audit]"
        elif entry.resolved_path is not None and entry.resolved_path != entry.path:
            audit += f" [build spelling: {comment_safe(entry.resolved_path)}]"
        if entry.classification in {"c", "asm"} and not entry.include_in_build:
            audit += " [excluded by Keil IncludeInBuild=0; not built by v1]"
        elif entry.classification == "c" and entry.include_in_build:
            unsafe_reason = entry.build_path_unsafe_reason()
            if unsafe_reason is not None:
                audit += f" [enabled but not built by v1: {comment_safe(unsafe_reason)}]"
        lines.append(audit)

    lines.extend([
        "",
        ".PHONY: all clean print-config",
        ".DELETE_ON_ERROR:",
        "",
        "print-config:",
        "\t@printf '%s\\n' 'project=$(PROJECT_DIR)' 'device=$(BOARD)' 'edata_end=$(EDATA_END)' 'cseg=$(CSEG_BASE)' 'xinit=$(XINIT_BASE)'",
        "",
        "$(BUILD):",
        "\tmkdir -p \"$@\"",
        "",
    ])

    if not project.buildable_c_files:
        lines.extend([
            "all:",
            "\t@printf '%s\\n' 'No enabled and representable C source in this .uvproj; inspect retained FILE entries.' >&2; false",
            "",
            *clean_recipe(),
        ])
        return "\n".join(lines)

    lines.extend([
        "# Numbered object names avoid collisions and preserve paths with spaces.",
        "# C_SOURCE_nnn keeps Make's escaped-space form for prerequisites;",
        "# C_SOURCE_RAW_nnn keeps the real path for quoted shell arguments.",
    ])
    object_names: list[str] = []
    for index, entry in enumerate(project.buildable_c_files):
        source_var = f"C_SOURCE_{index:03d}"
        raw_var = f"C_SOURCE_RAW_{index:03d}"
        object_name = f"$(BUILD)/c{index:03d}.o"
        object_names.append(object_name)
        path_for_make = make_path_escape(entry.resolved_path or "")
        lines.append(f"{source_var} := $(PROJECT_DIR_MAKE)/{path_for_make}")
        lines.append(f"{raw_var} := $(PROJECT_DIR)/{entry.resolved_path}")
    lines.extend([
        "OBJECTS := " + " ".join(object_names),
        "ELF_NOP_OBJECT := $(BUILD)/mcs251-porting-elf-nop.o",
        "# Keil assembly entries (not assembled by v1): "
        + (comment_safe(" ".join(x.path for x in project.asm_files)) or "<none>"),
        "",
        "all: $(BUILD)/firmware.hex",
        "",
        "# llc must emit ELF explicitly; default output is obsolete ASxxxx .rel.",
        "$(BUILD)/firmware.elf: $(OBJECTS) $(ELF_NOP_OBJECT) | $(BUILD)",
        "\t$(LLD) -flavor mcs251 --edata-end $(EDATA_END) \\",
        "\t  --area-start=HOME=0xff0000 --area-start=VECS=0xff0003 \\",
        "\t  --area-start=BOOT=0xff0100 --area-start=CSEG=$(CSEG_BASE) \\",
        "\t  --area-start=XINIT=$(XINIT_BASE) -o \"$@\" $^",
        "",
        "# The linker output is ELF; llvm-objcopy turns that ELF input into Intel HEX.",
        "$(BUILD)/firmware.hex: $(BUILD)/firmware.elf",
        "\t$(OBJCOPY) -O ihex \"$<\" \"$@\"",
        "",
        "$(ELF_NOP_OBJECT): $(ELF_NOP_YAML) | $(BUILD)",
        "\t$(YAML2OBJ) \"$<\" -o \"$@\"",
        "",
    ])
    for index, _entry in enumerate(project.buildable_c_files):
        source_var = f"C_SOURCE_{index:03d}"
        raw_var = f"C_SOURCE_RAW_{index:03d}"
        ll = f"$(BUILD)/c{index:03d}.ll"
        obj = f"$(BUILD)/c{index:03d}.o"
        lines.extend([
            f"{ll}: $({source_var}) | $(BUILD)",
            f"\t$(CLANG) $(CPPFLAGS) $(CFLAGS) -S -emit-llvm \"$({raw_var})\" -o \"$@\"",
            "",
            f"{obj}: {ll}",
            "\t$(LLC) -mtriple=mcs251-unknown-none -verify-machineinstrs \\",
            "\t  -mcs251-object-format=elf -filetype=obj \"$<\" -o \"$@\"",
            "",
        ])
    lines.extend(clean_recipe())
    return "\n".join(lines)


def distribution(values: Iterable[int]) -> dict[str, int]:
    counts = Counter(values)
    return {str(key): counts[key] for key in sorted(counts)}


def example_c_h_statistics(corpus_root: Path) -> dict[str, object]:
    bundles = sorted(path for path in corpus_root.iterdir() if path.is_dir())
    counts = [
        sum(1 for item in bundle.rglob("*") if item.is_file() and item.suffix.lower() in {".c", ".h"})
        for bundle in bundles
    ]
    single = sum(count == 1 for count in counts)
    return {
        "examples": len(bundles),
        "c_h_source_file_distribution": distribution(counts),
        "single_c_h_source_examples": single,
        "single_c_h_source_ratio": single / len(bundles) if bundles else 0.0,
        "multi_c_h_source_examples": len(bundles) - single,
    }


def count_entries(projects: list[Project], predicate: object) -> int:
    return sum(1 for project in projects for entry in project.files if predicate(entry))  # type: ignore[operator]


def summary(projects: list[Project], corpus_root: Path, output_root: Path) -> dict[str, object]:
    candidate_counts = [len(project.source_candidates) for project in projects]
    enabled_candidate_counts = [len(project.enabled_source_candidates) for project in projects]
    c_counts = [len(project.c_files) for project in projects]
    enabled_c_counts = [len(project.enabled_c_files) for project in projects]
    buildable_c_counts = [len(project.buildable_c_files) for project in projects]
    return {
        "format": "mcs251-uvproj2make-v1",
        "corpus_root": str(corpus_root),
        "generated_root": str(output_root),
        "projects": len(projects),
        "makefiles": len(projects),
        "parse_failures": [],
        "device_distribution": dict(sorted(Counter(project.device for project in projects).items())),
        "listed_source_candidate_distribution": distribution(candidate_counts),
        "enabled_source_candidate_distribution": distribution(enabled_candidate_counts),
        "listed_c_source_distribution": distribution(c_counts),
        "enabled_c_source_distribution": distribution(enabled_c_counts),
        "buildable_c_source_distribution": distribution(buildable_c_counts),
        "single_listed_source_projects": sum(count == 1 for count in candidate_counts),
        "single_listed_source_ratio": sum(count == 1 for count in candidate_counts) / len(projects) if projects else 0.0,
        "multi_listed_source_projects": sum(count != 1 for count in candidate_counts),
        "projects_with_assembly": sum(bool(project.asm_files) for project in projects),
        "projects_with_no_enabled_c_sources": sum(not project.enabled_c_files for project in projects),
        "projects_without_buildable_c_sources": sum(not project.buildable_c_files for project in projects),
        "listed_c_source_entries": sum(len(project.c_files) for project in projects),
        "enabled_c_source_entries": sum(len(project.enabled_c_files) for project in projects),
        "build_rule_c_source_entries": sum(len(project.buildable_c_files) for project in projects),
        "excluded_c_source_entries": count_entries(projects, lambda entry: entry.classification == "c" and not entry.include_in_build),
        "unresolved_c_source_entries": count_entries(projects, lambda entry: entry.classification == "c" and entry.resolved_path is None),
        "unrepresentable_c_source_entries": count_entries(
            projects,
            lambda entry: entry.classification == "c" and entry.include_in_build and entry.build_path_unsafe_reason() not in {None, "source not found in corpus"},
        ),
        "case_corrected_c_source_entries": count_entries(projects, lambda entry: entry.classification == "c" and entry.resolved_path is not None and entry.resolved_path != entry.path),
        "example_c_h_source_statistics": example_c_h_statistics(corpus_root),
        "note": (
            "Listed, enabled, and buildable source counts are distinct: IncludeInBuild=0 "
            "entries and enabled entries whose path v1 cannot represent remain audit data "
            "but are not build prerequisites."
        ),
        "project_records": [asdict(project) for project in projects],
        "generated_makefile_sha256": {
            project.slug: hashlib.sha256(
                (output_root / project.slug / "Makefile").read_bytes()
            ).hexdigest()
            for project in projects
            if (output_root / project.slug / "Makefile").is_file()
        },
    }


def write_tsv(projects: list[Project], output: Path) -> None:
    with output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow([
            "uvproj", "makefile", "target_name", "device", "board", "listed_files",
            "listed_c_sources", "enabled_c_sources", "buildable_c_sources", "excluded_c_sources",
            "asm_sources", "unresolved_c_sources", "unrepresentable_c_sources",
            "case_corrected_c_sources", "include_paths", "defines",
        ])
        for project in projects:
            writer.writerow([
                project.uvproj,
                f"{project.slug}/Makefile",
                project.target_name,
                project.device,
                project.board,
                len(project.files),
                len(project.c_files),
                len(project.enabled_c_files),
                len(project.buildable_c_files),
                sum(item.classification == "c" and not item.include_in_build for item in project.files),
                len(project.asm_files),
                sum(item.classification == "c" and item.resolved_path is None for item in project.files),
                sum(
                    item.classification == "c"
                    and item.include_in_build
                    and item.build_path_unsafe_reason() not in {None, "source not found in corpus"}
                    for item in project.files
                ),
                sum(item.classification == "c" and item.resolved_path is not None and item.resolved_path != item.path for item in project.files),
                ";".join(project.include_paths),
                ";".join(project.defines),
            ])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus_root", type=Path, help="root containing legacy .uvproj files")
    parser.add_argument("-o", "--output", required=True, type=Path, help="directory for generated Makefiles")
    parser.add_argument("--report", required=True, type=Path, help="write JSON project and aggregate report")
    parser.add_argument("--tsv", required=True, type=Path, help="write one TSV audit row per project")
    args = parser.parse_args()

    corpus_root = args.corpus_root.resolve()
    output = args.output.resolve()
    if not corpus_root.is_dir():
        raise ProjectError(f"corpus root is not a directory: {corpus_root}")
    uvprojs = sorted(corpus_root.rglob("*.uvproj"))
    if not uvprojs:
        raise ProjectError(f"no .uvproj files below {corpus_root}")

    projects: list[Project] = []
    errors: list[str] = []
    for path in uvprojs:
        try:
            projects.append(parse_project(path, corpus_root))
        except ProjectError as exc:
            errors.append(f"{path}: {exc}")
    if errors:
        print("uvproj2make: refusing partial output; parse failures:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1

    output.mkdir(parents=True, exist_ok=True)
    expected_slugs = {project.slug for project in projects}
    for stale in output.iterdir():
        if stale.is_dir() and stale.name not in expected_slugs:
            raise ProjectError(f"refusing to mix output with stale project directory: {stale}")
    for project in projects:
        makefile = output / project.slug / "Makefile"
        makefile.parent.mkdir(parents=True, exist_ok=True)
        makefile.write_text(render_makefile(project), encoding="utf-8", newline="\n")

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.tsv.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(summary(projects, corpus_root, output), indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    write_tsv(projects, args.tsv)
    print(f"parsed={len(projects)} makefiles={len(projects)} output={output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ProjectError as exc:
        print(f"uvproj2make: error: {exc}", file=sys.stderr)
        sys.exit(2)
