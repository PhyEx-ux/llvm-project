#!/usr/bin/env python3
"""Self-contained boundary and negative tests for the v1 porting tools.

Every input is embedded in this file or synthesized in a temporary directory.
The suite never reads ``~/stcex``, previously generated reports, or the frozen
toolchain binaries (the few checks that use them are skipped automatically when
the binaries are absent, e.g. on a host without the WSL environment).

It encodes the review's regression list: Make/shell injection (B1), fake-PASS
gates (B2), lexical SFR classification (M1), three counting bases (M2), build
selection semantics (M3), and SFR artifact cross-checking (M5).
"""

from __future__ import annotations

import importlib.util
import hashlib
import json
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CLANG = "/home/liu/build-clang/bin/clang"
# Frozen, hash-pinned binaries: the s1 tree is rebuilt in place and its
# binaries drift, so the optional tool-gated tests must not depend on it.
LLC = "/home/liu/mcs251-demo-test/bin-frozen/llc"
YAML2OBJ = "/home/liu/mcs251-demo-test/bin-frozen/yaml2obj"
BOARDS = ROOT.parent / "mcs251-demo-modern" / "boards"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    # Dataclasses with postponed annotations resolve their module through
    # sys.modules during class creation.
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        del sys.modules[name]
        raise
    return module


uvproj2make = load_module("uvproj2make_test", ROOT / "tools" / "uvproj2make.py")
sfr_convert = load_module("sfr_convert_test", ROOT / "tools" / "sfr-convert.py")
audit_intrinsics = load_module("audit_intrinsics_test", HERE / "audit-intrinsics.py")
verify_sfr = load_module("verify_sfr_artifacts_test", HERE / "verify-sfr-artifacts.py")
final_gate = load_module("assemble_final_validation_test", HERE / "assemble-final-validation.py")


UVPROJ = """<?xml version="1.0" encoding="{encoding}"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Targets>
    <Target>
      <TargetName>{target_name}</TargetName>
      <TargetOption>
        <TargetCommonOption>
          <Device>STC32G12K128 Series</Device>
          <IncludePath>{include_path}</IncludePath>
        </TargetCommonOption>
        <Target251>
          <C251>
            <VariousControls>
              <Define>{defines}</Define>
            </VariousControls>
          </C251>
        </Target251>
      </TargetOption>
      <Groups>
        <Group>
          <Files>
{file_elements}
          </Files>
        </Group>
      </Groups>
    </Target>
  </Targets>
</Project>
"""


def file_element(name: str, path: str, include_in_build: bool | None = None) -> str:
    option = ""
    if include_in_build is not None:
        option = (
            "<FileOption><CommonProperty>"
            f"<IncludeInBuild>{int(include_in_build)}</IncludeInBuild>"
            "</CommonProperty></FileOption>"
        )
    return (
        f"<File><FileName>{name}</FileName><FileType>1</FileType>"
        f"<FilePath>{path}</FilePath>{option}</File>"
    )


def write_uvproj(directory: Path, *, encoding: str = "utf-8", **fields) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    text = UVPROJ.format(encoding=encoding, **fields)
    path = directory / "sample.uvproj"
    path.write_bytes(text.encode(encoding))
    return path


def run_converter(corpus: Path, output: Path, report: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "uvproj2make.py"),
            str(corpus),
            "-o",
            str(output),
            "--report",
            str(report),
            "--tsv",
            str(report.with_suffix(".tsv")),
        ],
        text=True,
        capture_output=True,
        encoding="utf-8",
        check=False,
    )


def make_dry_run(project_dir: Path, corpus: Path, *goals: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "make",
            "--no-print-directory",
            "-C",
            str(project_dir),
            "-n",
            *goals,
            f"CORPUS_ROOT={corpus}",
            f"BOARD_ROOT={BOARDS}",
            f"ELF_NOP_YAML={ROOT / 'runtime' / 'elf-nop.yaml'}",
        ],
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def make_real_run(
    project_dir: Path,
    corpus: Path,
    *overrides: str,
    goals: tuple[str, ...] = ("all",),
) -> subprocess.CompletedProcess[str]:
    """Actually execute make (no -n) with stubbed tool overrides."""
    return subprocess.run(
        [
            "make",
            "--no-print-directory",
            "-C",
            str(project_dir),
            *goals,
            f"CORPUS_ROOT={corpus}",
            f"BOARD_ROOT={BOARDS}",
            f"ELF_NOP_YAML={ROOT / 'runtime' / 'elf-nop.yaml'}",
            *overrides,
        ],
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def clang_argument_vectors(dry_run_output: str) -> list[list[str]]:
    """Shell-split every clang command the dry run would execute."""
    vectors = []
    for line in dry_run_output.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("clang") or "/clang " in stripped:
            vectors.append(shlex.split(stripped))
    return vectors


def names(classification, category: str) -> set[str]:
    return {entry.name for entry in getattr(classification, category)}


# --------------------------------------------------------------------------
# M1: sfr-convert.py lexical classification
# --------------------------------------------------------------------------


class SfrConvertLexical(unittest.TestCase):
    def test_block_comment_and_literal_false_positives(self):
        source = (
            "/* sfr FAKE = 0x80; sbit FBIT = P0 ^ 1; */\n"
            'const char *s = "sfr STR = 0x91;";\n'
            "// sfr NOTE = 0x93;\n"
            "sfr REAL = 0x90;\n"
        )
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "direct_sfr"), {"REAL"})
        self.assertEqual(names(result, "sbit"), set())
        self.assertEqual(names(result, "xfr"), set())

    def test_line_spliced_line_comment_continues(self):
        source = "// comment continues \\\nsfr HIDDEN = 0x80;\nsfr VISIBLE = 0x81;\n"
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "direct_sfr"), {"VISIBLE"})

    def test_stray_quote_does_not_swallow_file(self):
        source = "sfr P0 = 0x80;\nchar broken = 'x; /* sfr GONE = 0x90; */\nsfr P1 = 0x81;\n"
        result = sfr_convert.classify(source)
        # The unterminated ' literal ends at its newline; the masked comment on
        # the same line is still a comment, and P1 survives on the next line.
        self.assertIn("P1", names(result, "direct_sfr"))

    def test_two_declarations_on_one_line(self):
        result = sfr_convert.classify("sfr A = 0x80; sfr B = 0x81;\n")
        self.assertEqual(names(result, "direct_sfr"), {"A", "B"})

    def test_xfr_with_trailing_comment_is_not_lost(self):
        source = (
            "#define DMA (*(unsigned char volatile far *)0x7EF000) /* cap */\n"
            "#define USB (*(unsigned char xdata volatile *)0xFE00) // cap\n"
        )
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "xfr"), {"DMA", "USB"})
        self.assertEqual(result.counts()["ignored"], 0)

    def test_duplicate_xfr_and_sbit_are_rejected_with_reason(self):
        source = (
            "#define DUP (*(unsigned char volatile far *)0x7EF001)\n"
            "#define DUP (*(unsigned char volatile far *)0x7EF002)\n"
            "sbit TWICE = P0 ^ 0;\n"
            "sbit TWICE = P0 ^ 1;\n"
        )
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "xfr"), {"DUP"})
        self.assertEqual(names(result, "sbit"), {"TWICE"})
        reasons = {entry.reason for entry in result.ignored}
        self.assertIn("duplicate XFR name", reasons)
        self.assertIn("duplicate sbit name", reasons)
        self.assertEqual(result.raw_declaration_counts, {"direct_sfr": 0, "xfr": 2, "sbit": 2})

    def test_if_zero_branch_is_inactive_but_recorded(self):
        source = "#if 0\nsfr ZERO = 0x80;\n#endif\nsfr ONE = 0x81;\n"
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "direct_sfr"), {"ONE"})
        self.assertEqual(names(result, "ignored"), {"<inactive>"})
        self.assertEqual(result.ignored[0].line, 2)

    def test_unknown_guard_keeps_first_branch(self):
        source = "#ifdef __KEIL__\nsfr KEEL = 0x80;\n#else\nsfr ELSE = 0x81;\n#endif\n"
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "direct_sfr"), {"KEEL"})

    def test_address_ranges(self):
        source = (
            "sfr LOW = 0x7F;\n"
            "sfr OK1 = 0x80;\n"
            "sfr OK2 = 0xFF;\n"
            "sfr HIGH = 0x100;\n"
            "sbit WIDE = P0 ^ 8;\n"
            "sfr16 W = 0x90;\n"
            "#define X1 (*(unsigned char volatile far *)0x7EF000)\n"
            "#define X2 (*(unsigned char volatile far *)0x7EFFFF)\n"
            "#define X3 (*(unsigned char volatile far *)0x7F0000)\n"
            "#define X4 (*(unsigned char volatile far *)0xFDFF)\n"
            "#define X5 (*(unsigned char volatile far *)0xFF00)\n"
        )
        result = sfr_convert.classify(source)
        self.assertEqual(names(result, "direct_sfr"), {"OK1", "OK2"})
        self.assertEqual(names(result, "xfr"), {"X1", "X2", "X5"})
        ignored_names = {entry.name for entry in result.ignored}
        self.assertEqual(ignored_names, {"LOW", "HIGH", "WIDE", "sfr16", "X3", "X4"})

    def test_unparsed_declaration_excerpt_is_comment_safe(self):
        result = sfr_convert.classify("sfr BAD = NOPE; /* boom */ ;\n")
        self.assertTrue(result.ignored)
        rendered = sfr_convert.render_header(
            Path("stc.h"), Path("out.h"), result
        )
        for line in rendered.splitlines():
            if line.startswith("/* ignored"):
                self.assertTrue(line.endswith("*/"))
                self.assertEqual(line.count("*/"), 1, line)

    def test_every_declaration_is_consumed_or_rejected(self):
        result = sfr_convert.classify("sfr A = 0x80;\nsfr WEIRD = ;\n")
        self.assertEqual(names(result, "direct_sfr"), {"A"})
        self.assertEqual(result.raw_declaration_counts["direct_sfr"], 1)
        self.assertTrue(
            any("unparsed Keil declaration" in (entry.reason or "") for entry in result.ignored),
            result.ignored,
        )


# --------------------------------------------------------------------------
# B1 + M3 + minor: uvproj2make.py Make/shell layering
# --------------------------------------------------------------------------


class Uvproj2MakeBoundary(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="mcs251-uv2mk-")
        self.tmp = Path(self._tmp.name)
        self.corpus = self.tmp / "corpus" / "proj"
        self.corpus.mkdir(parents=True, exist_ok=True)
        self.output = self.tmp / "generated"
        self.report = self.tmp / "uvproj-summary.json"
        self.addCleanup(self._tmp.cleanup)

    def generate(self, **fields) -> subprocess.CompletedProcess[str]:
        write_uvproj(self.corpus, **fields)
        return run_converter(self.corpus.parent, self.output, self.report)

    def test_b1_command_substitution_define_rejected(self):
        completed = self.generate(
            target_name="probe",
            include_path="",
            defines="VALUE=$(printf ALICE_PROBE)",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertNotEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertIn("unsafe Define", completed.stderr)

    def test_b1_quoted_value_with_space_rejected(self):
        completed = self.generate(
            target_name="probe",
            include_path="",
            defines='MESSAGE="hello world"',
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe Define", completed.stderr)

    def test_b1_backtick_include_path_rejected(self):
        completed = self.generate(
            target_name="probe",
            include_path="inc`id`",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe IncludePath", completed.stderr)

    def test_b1_hash_include_path_rejected_and_newline_splits_safely(self):
        completed = self.generate(
            target_name="probe",
            include_path="a#b",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsafe IncludePath", completed.stderr)
        # A newline entity cannot smuggle a second shell word into one element:
        # Keil list splitting treats it as a separator before any validation.
        (self.corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="probe2",
            include_path="alpha&#10;beta",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        text = next(self.output.rglob("Makefile")).read_text(encoding="utf-8")
        self.assertIn("'-I$(PROJECT_DIR)/alpha'", text)
        self.assertIn("'-I$(PROJECT_DIR)/beta'", text)

    def test_b1_space_include_path_is_one_shell_argument(self):
        (self.corpus / "inc lib").mkdir(parents=True, exist_ok=True)
        (self.corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="space include",
            include_path="inc lib",
            defines="STC32G,FREQ=11059200",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        makefile = self.output / self._slug() / "Makefile"
        text = makefile.read_text(encoding="utf-8")
        self.assertIn("CPPFLAGS += '-I$(PROJECT_DIR)/inc lib'", text)
        self.assertIn("CPPFLAGS += '-DFREQ=11059200'", text)
        self.assertNotIn("ALICE_PROBE", text)
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        dry = make_dry_run(makefile.parent, self.corpus.parent)
        self.assertEqual(dry.returncode, 0, dry.stderr)
        vectors = clang_argument_vectors(dry.stdout)
        self.assertTrue(vectors, dry.stdout)
        expected = f"-I{self.corpus}/inc lib"
        self.assertTrue(any(expected in vector for vector in vectors), vectors)

    def test_b1_space_source_path_is_one_shell_argument(self):
        source_dir = self.corpus / "my dir"
        source_dir.mkdir(parents=True, exist_ok=True)
        (source_dir / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="space source",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "my dir\\main.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        makefile = next(self.output.iterdir()) / "Makefile"
        text = makefile.read_text(encoding="utf-8")
        self.assertIn('C_SOURCE_000 := $(PROJECT_DIR_MAKE)/my\\ dir/main.c', text)
        self.assertIn('C_SOURCE_RAW_000 := $(PROJECT_DIR)/my dir/main.c', text)
        self.assertIn('"$(C_SOURCE_RAW_000)"', text)
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        dry = make_dry_run(makefile.parent, self.corpus.parent)
        self.assertEqual(dry.returncode, 0, dry.stderr)
        expected = str(self.corpus / "my dir" / "main.c")
        for vector in clang_argument_vectors(dry.stdout):
            self.assertIn(expected, vector, vector)
            self.assertTrue(all("my\\ dir" not in item for item in vector), vector)

    def test_m3_include_in_build_zero_excluded_from_build(self):
        (self.corpus / "good.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        (self.corpus / "3rd").mkdir(parents=True, exist_ok=True)
        (self.corpus / "3rd" / "song.c").write_text("int song(void){return 1;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="exclusion",
            include_path="",
            defines="",
            file_elements=(
                file_element("good.c", "good.c")
                + file_element("song.c", "3rd/song.c", include_in_build=False)
            ),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        summary = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(summary["enabled_c_source_entries"], 1)
        self.assertEqual(summary["build_rule_c_source_entries"], 1)
        self.assertEqual(summary["excluded_c_source_entries"], 1)
        makefile = next(path for path in self.output.rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"^C_SOURCE_RAW_\d{3} :=", text, re.MULTILINE)), 1)
        self.assertIn("3rd/song.c", text)  # retained as audit comment
        self.assertIn("excluded by Keil IncludeInBuild=0", text)
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        dry = make_dry_run(makefile.parent, self.corpus.parent)
        self.assertEqual(dry.returncode, 0, dry.stderr)
        self.assertNotIn("song.c", dry.stdout)
        self.assertIn("good.c", dry.stdout)

    def test_unrepresentable_source_path_stays_audit_only(self):
        (self.corpus / "we$ird.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="unsafe path",
            include_path="",
            defines="",
            file_elements=file_element("we$ird.c", "we$ird.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        summary = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(summary["unrepresentable_c_source_entries"], 1)
        self.assertEqual(summary["build_rule_c_source_entries"], 0)
        text = next(self.output.rglob("Makefile")).read_text(encoding="utf-8")
        self.assertNotIn("C_SOURCE_RAW_000", text)
        self.assertIn("enabled but not built by v1", text)

    def test_target_name_newline_cannot_inject_make_syntax(self):
        (self.corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="T1\nEVIL := $(shell id)",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        text = next(self.output.rglob("Makefile")).read_text(encoding="utf-8")
        self.assertNotRegex(text, r"^EVIL :=", msg="comment interpolation escaped its line")

    def test_project_directory_with_make_metachar_rejected(self):
        evil = self.corpus.parent / "ba$d"
        write_uvproj(
            evil,
            target_name="x",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "missing.c"),
        )
        completed = run_converter(self.corpus.parent, self.output, self.report)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsupported character", completed.stderr)

    def test_project_directory_with_make_rule_delimiters_rejected(self):
        # The project directory layer must reject the same Make rule-syntax
        # delimiters as source paths: it is expanded inside prerequisite
        # variables too. ';' would become an inline recipe once expanded.
        for name in ("se;mi", "co:lon", "pi|pe"):
            evil = self.corpus.parent / name
            write_uvproj(
                evil,
                target_name="x",
                include_path="",
                defines="",
                file_elements=file_element("main.c", "missing.c"),
            )
        completed = run_converter(self.corpus.parent, self.output, self.report)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("unsupported character", completed.stderr)
        for name in (";", ":", "|"):
            self.assertIn(name, completed.stderr)

    def test_b1_make_metachar_source_paths_stay_audit_only(self):
        # ':', '|', TAB, and '=' are legal filename characters that break Make
        # rule syntax when a prerequisite variable expands: ':' resplits the
        # rule, '|' starts order-only prerequisites, TAB splits prerequisite
        # words, and '=' parses as a target-specific variable assignment.
        names = ("co:lon.c", "pi|pe.c", "ta\tb.c", "eq=uals.c")
        for name in names:
            (self.corpus / name).write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="metachar paths",
            include_path="",
            defines="",
            file_elements="".join(file_element(name, name) for name in names),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        summary = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(summary["build_rule_c_source_entries"], 0)
        self.assertEqual(summary["unrepresentable_c_source_entries"], 4)
        makefile = next(path for path in self.output.rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8")
        self.assertEqual(re.findall(r"^C_SOURCE_RAW_\d{3} :=", text, re.MULTILINE), [])
        self.assertEqual(re.findall(r"^C_SOURCE_\d{3} :=", text, re.MULTILINE), [])
        for name in names:
            # comment_safe replaces non-printable characters (e.g. TAB) with
            # spaces in audit comments; assert the sanitized spelling appears.
            self.assertIn(uvproj2make.comment_safe(name), text)
        self.assertEqual(text.count("enabled but not built by v1"), 4)
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        dry = make_dry_run(makefile.parent, self.corpus.parent)
        self.assertEqual(dry.returncode, 0, dry.stderr)
        for name in names:
            self.assertNotIn(name, dry.stdout)

    def test_b1_semicolon_source_path_cannot_execute_through_make(self):
        # B1 residue reproduction: a source file named "main;<cmd>;.c" used
        # to be accepted and expanded into the prerequisite line, where GNU
        # make reads the semicolon as an inline recipe separator and really
        # executed the injected text. The bare ">marker" redirection needs no
        # space, quote, or '$' and therefore survives every earlier filter.
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        marker = "semaphore-of-injection"
        poisoned = f"main;>{marker};.c"
        # The prerelease converter expanded the poisoned prerequisite to
        # ".../main"; a plain "main" file let make reach and run the injected
        # inline recipe. It stays in the fixture so the execution path exists.
        (self.corpus / "main").write_text("int main(void){return 0;}\n", encoding="utf-8")
        (self.corpus / poisoned).write_text("int x(void){return 1;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="semicolon injection",
            include_path="",
            defines="",
            file_elements=file_element(poisoned, poisoned),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        summary = json.loads(self.report.read_text(encoding="utf-8"))
        self.assertEqual(summary["build_rule_c_source_entries"], 0)
        self.assertEqual(summary["unrepresentable_c_source_entries"], 1)
        makefile = next(path for path in self.output.rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8")
        # No extra recipe is generated for the unrepresentable entry at all.
        self.assertNotIn("C_SOURCE_RAW_000", text)
        self.assertNotIn("C_SOURCE_000", text)
        self.assertIn("enabled but not built by v1", text)
        sentinel = self.tmp / "clang-sentinel.sh"
        log = self.tmp / "clang-sentinel.log"
        sentinel.write_text(f'#!/bin/sh\nprintf invoked >> "{log}"\nexit 0\n', encoding="utf-8")
        sentinel.chmod(0o755)
        run = make_real_run(
            makefile.parent,
            self.corpus.parent,
            f"CLANG={sentinel}",
            "LLC=true",
            "LLD=true",
            "OBJCOPY=true",
            "YAML2OBJ=true",
        )
        combined = run.stdout + run.stderr
        self.assertNotEqual(run.returncode, 0, combined)  # all: fails on no representable source
        self.assertIn("No enabled and representable C source", combined)
        # The injected inline recipe never executed and no compiler ran.
        self.assertEqual(list(makefile.parent.glob(marker)), [], "injected Make recipe executed")
        self.assertFalse(log.exists(), "CLANG sentinel was invoked for an unrepresentable source")

    def test_gb2312_project_is_supported(self):
        (self.corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="gb2312",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "main.c"),
            encoding="gb2312",
        )
        # The template is ASCII-compatible; gb2312 exercises the decode path.
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(any(self.output.rglob("Makefile")))

    def test_default_gb2312_bytes_with_chinese_device_path(self):
        directory = self.corpus / "器件"
        directory.mkdir(parents=True, exist_ok=True)
        text = UVPROJ.format(
            encoding="gb2312",
            target_name="中文目标",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        (directory / "sample.uvproj").write_bytes(text.encode("gb2312"))
        completed = run_converter(self.corpus, self.output, self.report)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        makefile = next(self.output.rglob("Makefile")).read_text(encoding="utf-8")
        self.assertIn("中文目标", makefile)

    def test_clean_recipe_is_boundary_guarded(self):
        (self.corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        completed = self.generate(
            target_name="guard",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        text = next(self.output.rglob("Makefile")).read_text(encoding="utf-8")
        self.assertIn("refusing to clean BUILD outside this generated project", text)
        self.assertIn(".DEFAULT_GOAL := all", text)
        self.assertIn(".NOTPARALLEL:", text)

    def _slug(self) -> str:
        return uvproj2make.stable_slug("proj/sample.uvproj")


# --------------------------------------------------------------------------
# M2: audit-intrinsics.py three counting bases
# --------------------------------------------------------------------------


class AuditIntrinsicsBases(unittest.TestCase):
    SNIPPET = (
        "#include <intrins.h>\n"
        "void f(void) {\n"
        "    _nop_();\n"          # real call
        "    // _nop_();\n"       # line comment
        "    /* _nop_(); */\n"    # block comment
        '    char *s = "_nop_()";\n'  # string literal
        "    _crol_(1);\n"        # unsupported intrinsic, real call
        "}\n"
        "#if 0\n"
        "_nop_();\n"              # definitely inactive branch
        "#endif\n"
    )

    def count(self, text: str) -> dict[str, int]:
        calls: dict[str, int] = {}
        for match in audit_intrinsics.CALL_RE.finditer(text):
            calls[match.group("name")] = calls.get(match.group("name"), 0) + 1
        return calls

    def test_three_bases_are_distinct(self):
        raw = self.count(self.SNIPPET)
        lexical_text = audit_intrinsics.mask_comments_and_literals(self.SNIPPET)
        lexical = self.count(lexical_text)
        active = self.count(audit_intrinsics.mask_definitely_inactive_branches(lexical_text))
        self.assertEqual(raw, {"_nop_": 5, "_crol_": 1})
        self.assertEqual(lexical, {"_nop_": 2, "_crol_": 1})
        self.assertEqual(active, {"_nop_": 1, "_crol_": 1})

    def test_masked_text_preserves_offsets(self):
        masked = audit_intrinsics.mask_comments_and_literals(self.SNIPPET)
        self.assertEqual(len(masked), len(self.SNIPPET))
        self.assertEqual(masked.count("\n"), self.SNIPPET.count("\n"))

    def test_commented_seven_call_repro_is_not_lexical(self):
        # Shape of the real corpus find: commented-out calls must not count
        # in the lexical candidate base even though raw grep finds them.
        commented = "\n".join(f"    // _nop_();  {i}" for i in range(7))
        self.assertEqual(self.count(commented), {"_nop_": 7})
        lexical = audit_intrinsics.mask_comments_and_literals(commented)
        self.assertEqual(self.count(lexical), {})


# --------------------------------------------------------------------------
# M5: verify-sfr-artifacts.py rejects tampered artifacts
# --------------------------------------------------------------------------


SFR_SNIPPET = (
    "sfr P0 = 0x80;\n"
    "sfr P1 = 0x90;\n"
    "sbit P00 = P0 ^ 0;\n"
    "#define DMA (*(unsigned char volatile far *)0x7EF000)\n"
    "#if 0\nsfr GHOST = 0x81;\n#endif\n"
)


class VerifySfrArtifacts(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="mcs251-sfr-")
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.source = self.tmp / "device.h"
        self.source.write_text(SFR_SNIPPET, encoding="utf-8")
        self.classification = sfr_convert.classify(SFR_SNIPPET)
        self.header = self.tmp / "device-v1.h"
        self.report_path = self.tmp / "device-report.json"
        self.check = self.tmp / "selfcheck.c"
        self.regenerate()

    def regenerate(self) -> None:
        self.header.write_text(
            sfr_convert.render_header(self.source, self.header, self.classification),
            encoding="utf-8",
        )
        self.report_path.write_text(
            json.dumps(sfr_convert.report_object(self.source, self.header, self.classification), indent=2) + "\n",
            encoding="utf-8",
        )
        uses = "\n".join(f"    sink ^= {e.name};" for e in self.classification.direct_sfr)
        self.check.write_text(
            "volatile unsigned char sink;\nint main(void) {\n" + uses + "\n    return 0;\n}\n",
            encoding="utf-8",
        )

    def test_untampered_artifacts_pass(self):
        result = verify_sfr.verify_one(self.report_path, self.header, self.check)
        self.assertEqual(result["rendered_direct_macros"], 2)
        self.assertEqual(result["rendered_xfr_comments"], 1)

    def test_deleting_all_xfr_comments_is_rejected(self):
        text = self.header.read_text(encoding="utf-8")
        self.header.write_text(
            "\n".join(line for line in text.splitlines() if not line.startswith("/* XFR")) + "\n",
            encoding="utf-8",
        )
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)

    def test_duplicated_direct_macro_is_rejected(self):
        text = self.header.read_text(encoding="utf-8")
        duplicate = "#define P0 (*(volatile unsigned char *)0x80)"
        self.header.write_text(text.replace("#endif", duplicate + "\n#endif"), encoding="utf-8")
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)

    def test_report_count_mismatch_is_rejected(self):
        report = json.loads(self.report_path.read_text(encoding="utf-8"))
        report["counts"]["direct_sfr"] = 3
        self.report_path.write_text(json.dumps(report), encoding="utf-8")
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)

    def test_source_identity_mismatch_is_rejected(self):
        self.source.write_text(SFR_SNIPPET + "sfr P3 = 0xB0;\n", encoding="utf-8")
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)

    def test_selfcheck_missing_a_register_is_rejected(self):
        self.check.write_text(
            "volatile unsigned char sink;\nint main(void) {\n    sink ^= P0;\n    return 0;\n}\n",
            encoding="utf-8",
        )
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)

    def test_two_inactive_registrations_are_accepted(self):
        # M5 misrejection shape: two legal "#if 0" registrations are both
        # named <inactive> by the converter. They are audit records, not
        # mappings, so repeated placeholder names must not be folded into a
        # "duplicate ignored names" rejection of untampered artifacts.
        source = (
            "sfr P0 = 0x80;\n"
            "#if 0\nsfr G1 = 0x81;\n#endif\n"
            "#if 0\nsfr G2 = 0x82;\n#endif\n"
        )
        self.source.write_text(source, encoding="utf-8")
        self.classification = sfr_convert.classify(source)
        self.regenerate()
        ignored_names = [entry.name for entry in self.classification.ignored]
        self.assertEqual(ignored_names, ["<inactive>", "<inactive>"])
        result = verify_sfr.verify_one(self.report_path, self.header, self.check)
        self.assertEqual(result["counts"]["ignored"], 2)
        self.assertEqual(result["rendered_direct_macros"], 1)

    def test_duplicate_effective_mapping_in_report_is_rejected(self):
        # Exempting ignored audit records must not weaken uniqueness for the
        # effective mapping categories: a tampered report that duplicates an
        # xfr entry (counts adjusted to match) is still rejected.
        report = json.loads(self.report_path.read_text(encoding="utf-8"))
        self.assertTrue(report["xfr"])
        report["xfr"].append(dict(report["xfr"][0]))
        report["counts"]["xfr"] = len(report["xfr"])
        self.report_path.write_text(json.dumps(report), encoding="utf-8")
        with self.assertRaises(verify_sfr.VerificationError):
            verify_sfr.verify_one(self.report_path, self.header, self.check)


# --------------------------------------------------------------------------
# B2: assemble-final-validation.py gate rejects every fake-PASS shape
# --------------------------------------------------------------------------


def build_elf32_msb(text: bytes) -> bytes:
    shstr = b"\0.text\0.shstrtab\0"
    text_offset = 52
    shstr_offset = text_offset + len(text)
    shoffset = shstr_offset + len(shstr)
    header = (
        b"\x7fELF" + bytes([1, 2, 1, 0, 0]) + b"\0" * 7
        + struct.pack(
            ">HHIIIIIHHHHHH", 1, 0x51, 1, 0, 0, shoffset, 0, 52, 0, 0, 40, 3, 2
        )
    )

    def section(name, kind, flags, offset, size):
        return struct.pack(">IIIIIIIIII", name, kind, flags, 0, offset, size, 0, 0, 1, 0)

    return (
        header
        + text
        + shstr
        + section(0, 0, 0, 0, 0)
        + section(1, 1, 6, text_offset, len(text))
        + section(7, 3, 0, shstr_offset, len(shstr))
    )


class FinalValidationGate(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="mcs251-gate-")
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_negative_report_pass_is_rejected(self):
        with self.assertRaises(RuntimeError):
            final_gate.require_passed({"passed": False}, "stub")
        with self.assertRaises(RuntimeError):
            final_gate.require_passed({}, "stub")

    def test_smoke_with_wrong_magic_is_rejected(self):
        report = {
            "passed": True,
            "steps": [{"returncode": 0}],
            "object_magic": "deadbeef",
            "elf_magic": "7f454c46",
            "hex_starts_with_colon": True,
        }
        with self.assertRaises(RuntimeError):
            final_gate.smoke_status(report, "stub")
        report["object_magic"] = "7f454c46"
        report["steps"] = [{"returncode": 1}]
        with self.assertRaises(RuntimeError):
            final_gate.smoke_status(report, "stub")

    def test_b2_smoke_steps_null_is_rejected(self):
        # B2 fake-PASS shape 2: smoke report with steps:[null]. The old gate
        # filtered non-dict elements and then called any() on the empty list,
        # so [null] passed. require_step_records must reject each non-dict or
        # field-incomplete step as a failure, not skip it.
        report = {
            "passed": True,
            "steps": [None],
            "object_magic": "7f454c46",
            "elf_magic": "7f454c46",
            "hex_starts_with_colon": True,
        }
        with self.assertRaises(RuntimeError):
            final_gate.smoke_status(report, "stub")
        # A step dict missing required fields is also rejected, not skipped.
        report["steps"] = [{"returncode": 0}]
        with self.assertRaises(RuntimeError):
            final_gate.smoke_status(report, "stub")

    def test_b2_dry_run_zero_coverage_is_rejected(self):
        # B2 fake-PASS shape 1: dry-run report with {checked:0, passed:0}.
        # The old gate only compared checked==passed and accepted this. The
        # count must be bound to the actual generated Makefile set.
        root = self.make_project_tree()
        summary = json.loads((root / "reports" / "uvproj-summary.json").read_text(encoding="utf-8"))
        coverage = final_gate.verify_generated_makefiles(root, summary)
        zero_report = {
            "format": "mcs251-porting-makefile-validation-v1",
            "makefiles_checked": 0,
            "makefiles_passed": 0,
            "makefiles_failed": [],
            "sample_inspections": [],
        }
        with self.assertRaises(RuntimeError):
            final_gate.dry_run_coverage(zero_report, summary, coverage)
        # A report whose count does not match this generation is also rejected.
        mismatch_report = dict(zero_report, makefiles_checked=999, makefiles_passed=999)
        with self.assertRaises(RuntimeError):
            final_gate.dry_run_coverage(mismatch_report, summary, coverage)

    def test_elf_nop_parser_accepts_and_rejects(self):
        good = self.tmp / "good.o"
        good.write_bytes(build_elf32_msb(b"\x00\xaa"))
        text, count = final_gate.elf_text_and_nop_count(good)
        self.assertEqual(text, b"\x00\xaa")
        self.assertEqual(count, 1)
        bad = self.tmp / "bad.o"
        bad.write_bytes(build_elf32_msb(b"\x00\xbb"))
        text, count = final_gate.elf_text_and_nop_count(bad)
        self.assertNotEqual((text, count), (b"\x00\xaa", 1))
        garbage = self.tmp / "garbage.o"
        garbage.write_bytes(b"not-an-elf-at-all")
        with self.assertRaises(RuntimeError):
            final_gate.elf_text_and_nop_count(garbage)

    @unittest.skipUnless(Path(YAML2OBJ).is_file(), "frozen yaml2obj not available")
    def test_tampered_nop_yaml_is_rejected(self):
        root = self.tmp / "root"
        (root / "runtime").mkdir(parents=True)
        yaml_text = (ROOT / "runtime" / "elf-nop.yaml").read_text(encoding="utf-8")
        (root / "runtime" / "elf-nop.yaml").write_text(
            yaml_text.replace("Content: '00AA'", "Content: '00BB'"), encoding="utf-8"
        )
        work = self.tmp / "work"
        work.mkdir()
        with self.assertRaises(RuntimeError):
            final_gate.verify_real_elf_nop(root, work, YAML2OBJ)

    def make_project_tree(self) -> Path:
        corpus = self.tmp / "corpus" / "proj"
        corpus.mkdir(parents=True, exist_ok=True)
        (corpus / "main.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        write_uvproj(
            corpus,
            target_name="gate",
            include_path="",
            defines="",
            file_elements=file_element("main.c", "main.c"),
        )
        root = self.tmp / "root"
        report = root / "reports" / "uvproj-summary.json"
        completed = run_converter(corpus.parent, root / "generated" / "projects", report)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return root

    def make_project_tree_with_audit_only(self) -> Path:
        """One legal C source plus one enabled-but-unrepresentable source.

        Reproduces Alice's three-round recheck shape: ``good.c`` is
        buildable while ``main;>semaphore;.c`` is enabled in Keil but its
        path contains a Make rule-syntax character (``;``) that the
        converter refuses to render, downgrading it to "registered but not
        built". The converter exits 0 with enabled=2 / build_rule=1 /
        unrepresentable=1.
        """
        corpus = self.tmp / "corpus" / "proj"
        corpus.mkdir(parents=True, exist_ok=True)
        (corpus / "good.c").write_text("int main(void){return 0;}\n", encoding="utf-8")
        poisoned = "main;>semaphore;.c"
        (corpus / poisoned).write_text("int x(void){return 1;}\n", encoding="utf-8")
        write_uvproj(
            corpus,
            target_name="audit-only path",
            include_path="",
            defines="",
            file_elements=(
                file_element("good.c", "good.c")
                + file_element(poisoned, poisoned)
            ),
        )
        root = self.tmp / "root"
        report = root / "reports" / "uvproj-summary.json"
        completed = run_converter(corpus.parent, root / "generated" / "projects", report)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        summary = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(summary["enabled_c_source_entries"], 2)
        self.assertEqual(summary["build_rule_c_source_entries"], 1)
        self.assertEqual(summary["unrepresentable_c_source_entries"], 1)
        return root

    def test_makefile_set_mismatch_is_rejected(self):
        root = self.make_project_tree()
        summary = json.loads((root / "reports" / "uvproj-summary.json").read_text(encoding="utf-8"))
        shutil.rmtree(next((root / "generated" / "projects").iterdir()))
        with self.assertRaises(RuntimeError):
            final_gate.verify_generated_makefiles(root, summary)
        root2 = self.make_project_tree()
        extra = next((root2 / "generated" / "projects").iterdir())
        (extra / "stale").mkdir()
        (extra / "stale" / "Makefile").write_text("all:\n", encoding="utf-8")
        summary2 = json.loads((root2 / "reports" / "uvproj-summary.json").read_text(encoding="utf-8"))
        with self.assertRaises(RuntimeError):
            final_gate.verify_generated_makefiles(root2, summary2)

    def test_makefile_content_tampering_is_rejected(self):
        root = self.make_project_tree()
        summary_path = root / "reports" / "uvproj-summary.json"
        makefile = next((root / "generated" / "projects").rglob("Makefile"))
        makefile.write_text(
            makefile.read_text(encoding="utf-8") + "# tampered after generation\n",
            encoding="utf-8",
        )
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        with self.assertRaises(RuntimeError):  # hash no longer matches report
            final_gate.verify_generated_makefiles(root, summary)

    def test_missing_elf_flag_in_one_rule_is_rejected(self):
        root = self.make_project_tree()
        summary_path = root / "reports" / "uvproj-summary.json"
        makefile = next((root / "generated" / "projects").rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8").replace("-mcs251-object-format=elf ", "")
        makefile.write_text(text, encoding="utf-8")
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        slug = summary["project_records"][0]["slug"]
        summary["generated_makefile_sha256"][slug] = hashlib.sha256(text.encode()).hexdigest()
        summary_path.write_text(json.dumps(summary), encoding="utf-8")
        with self.assertRaises(RuntimeError):
            final_gate.verify_generated_makefiles(root, summary)

    def test_extra_build_rule_is_rejected(self):
        root = self.make_project_tree()
        summary_path = root / "reports" / "uvproj-summary.json"
        makefile = next((root / "generated" / "projects").rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8") + (
            "C_SOURCE_RAW_001 := $(PROJECT_DIR)/ghost.c\n"
        )
        makefile.write_text(text, encoding="utf-8")
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        slug = summary["project_records"][0]["slug"]
        summary["generated_makefile_sha256"][slug] = hashlib.sha256(text.encode()).hexdigest()
        summary_path.write_text(json.dumps(summary), encoding="utf-8")
        with self.assertRaises(RuntimeError):
            final_gate.verify_generated_makefiles(root, summary)

    def test_audit_only_unsafe_path_passes_final_gate(self):
        # Alice's three-round recheck: a project with one legal C source
        # plus one enabled-but-unrepresentable C source (audit only, no
        # build rule) must pass the final gate's build-rule comparison.
        # The converter downgrades the unsafe path to "registered but not
        # built"; buildable_c_entries must agree and not expect a
        # C_SOURCE_RAW assignment for it, so no false "build rule set does
        # not match project record" rejection.
        root = self.make_project_tree_with_audit_only()
        summary_path = root / "reports" / "uvproj-summary.json"
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        makefile = next((root / "generated" / "projects").rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8")
        # The audit-only file gets no raw-source assignment; exactly one
        # buildable rule exists (good.c).
        self.assertEqual(len(re.findall(r"^C_SOURCE_RAW_\d{3} :=", text, re.MULTILINE)), 1)
        self.assertIn("enabled but not built by v1", text)
        coverage = final_gate.verify_generated_makefiles(root, summary)
        self.assertEqual(coverage["build_c_rules"], 1)
        # Full chain: dry-run must pass with only good.c compiled.
        if not shutil.which("make"):
            self.skipTest("GNU make not available")
        dry = make_dry_run(makefile.parent, self.tmp / "corpus")
        self.assertEqual(dry.returncode, 0, dry.stderr)
        self.assertIn("good.c", dry.stdout)
        self.assertNotIn("semaphore", dry.stdout)

    def test_missing_build_rule_still_rejected_with_audit_only(self):
        # Security must not weaken: even when a project mixes a buildable
        # source with an audit-only (unrepresentable) source, dropping the
        # buildable file's C_SOURCE_RAW assignment must still be rejected.
        root = self.make_project_tree_with_audit_only()
        summary_path = root / "reports" / "uvproj-summary.json"
        makefile = next((root / "generated" / "projects").rglob("Makefile"))
        text = makefile.read_text(encoding="utf-8")
        removed = re.sub(r"^C_SOURCE_RAW_000 := .+\n", "", text, count=1, flags=re.MULTILINE)
        self.assertNotEqual(removed, text, "no C_SOURCE_RAW_000 line to remove")
        makefile.write_text(removed, encoding="utf-8")
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        slug = summary["project_records"][0]["slug"]
        summary["generated_makefile_sha256"][slug] = hashlib.sha256(removed.encode()).hexdigest()
        summary_path.write_text(json.dumps(summary), encoding="utf-8")
        with self.assertRaises(RuntimeError):
            final_gate.verify_generated_makefiles(root, summary)

    @unittest.skipUnless(Path(CLANG).is_file() and Path(LLC).is_file(), "frozen clang/llc not available")
    def test_failing_selfcheck_source_is_rejected(self):
        tests = self.tmp / "root2" / "tests"
        tests.mkdir(parents=True)
        (tests / "bad.c").write_text("int broken(\n", encoding="utf-8")
        (self.tmp / "work").mkdir(exist_ok=True)
        with self.assertRaises(RuntimeError):
            final_gate.compile_sfr_selfchecks(
                self.tmp / "root2", self.tmp / "work", CLANG, LLC, stems=("bad",)
            )

    @unittest.skipUnless(Path(CLANG).is_file() and Path(LLC).is_file(), "frozen clang/llc not available")
    def test_valid_selfcheck_produces_ir_identity(self):
        tests = self.tmp / "root3" / "tests"
        tests.mkdir(parents=True)
        (tests / "ok.c").write_text("int ok(void) { return 0; }\n", encoding="utf-8")
        (self.tmp / "work3").mkdir(exist_ok=True)
        result = final_gate.compile_sfr_selfchecks(
            self.tmp / "root3", self.tmp / "work3", CLANG, LLC, stems=("ok",)
        )
        self.assertTrue(result[0]["ir_sha256"])

    def test_empty_selfcheck_output_is_rejected(self):
        # Real negative: a fake clang that exits 0 but writes an empty .ll
        # file must be rejected by compile_sfr_selfchecks, not accepted as a
        # vacuous pass.
        tests = self.tmp / "root4" / "tests"
        tests.mkdir(parents=True)
        (tests / "ok.c").write_text("int ok(void) { return 0; }\n", encoding="utf-8")
        work = self.tmp / "work4"
        work.mkdir(exist_ok=True)
        fake_clang = self.tmp / "fake-clang.sh"
        fake_clang.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        fake_clang.chmod(0o755)
        with self.assertRaises(RuntimeError) as ctx:
            final_gate.compile_sfr_selfchecks(
                self.tmp / "root4", work, str(fake_clang), "/bin/true", stems=("ok",)
            )
        self.assertIn("no usable output", str(ctx.exception))


if __name__ == "__main__":
    unittest.main(verbosity=2)
