#!/usr/bin/env python3
"""BT06 X4 extension self-tests for the DEF.H type compat layer.

Runs without a compiler for the data/consistency checks, and (optionally)
with a provided clang for the compile-time behaviour checks.

Every ``check()`` below is a real assertion: it inspects actual return
codes, stderr text, or parsed artifacts, and any failure makes this
script exit non-zero.  The crafted-bad-header cases prove --check-header
is not vacuous (a tampered header must FAIL the byte-exact
regeneration), mirroring the BT06 test-bit-compat.py discipline.

Usage:
    python3 tests/test-type-compat.py                 # data checks only
    python3 tests/test-type-compat.py --clang <path>  # + compile behaviour
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEDGER_PATH = ROOT / "type-compat.json"
HEADER_PATH = ROOT / "include" / "mcs251_type_compat.h"
GEN = ROOT / "tools" / "gen-type-compat-header.py"
OFFICIAL_DEF_H = Path(
    "/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/COMM/DEF.H")


def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gen = _load_module(GEN, "gen_type_compat_header")

failures: list[str] = []
checks = 0


def check(cond: bool, msg: str) -> None:
    global checks
    checks += 1
    if not cond:
        failures.append(msg)
        print("FAIL: %s" % msg)


def norm(s: str) -> str:
    return re.sub(r"\s+", "", s)


def run_generator(args, expect_rc=0):
    p = subprocess.run([sys.executable, str(GEN)] + args,
                       capture_output=True, text=True)
    check(p.returncode == expect_rc,
          "generator %s rc=%d (expected %d): %s"
          % (args, p.returncode, expect_rc, p.stderr.strip()))
    return p


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--clang", type=Path, default=None,
                    help="optional clang for compile behaviour checks")
    args = ap.parse_args()

    # ------------------------------------------------------------------
    # 1. Ledger shape: totals pinned, every entry accounted.
    # ------------------------------------------------------------------
    data = json.loads(LEDGER_PATH.read_text(encoding="utf-8"))
    entries = data["entries"]
    by_name = {e["name"]: e for e in entries}
    check(len(entries) == 75, "ledger has %d entries, expected 75"
          % len(entries))
    check(len(by_name) == len(entries), "duplicate ledger names")
    mapped_td = [e for e in entries if e["kind"] == "typedef"
                 and e["disposition"] == "mapped"]
    mapped_mac = [e for e in entries if e["kind"] == "macro"
                  and e["disposition"] == "mapped"]
    external = [e for e in entries if e["disposition"] == "external"]
    rejected = [e for e in entries if e["disposition"] == "rejected"]
    check(len(mapped_td) == 18, "%d mapped typedefs, expected 18"
          % len(mapped_td))
    check(len(mapped_mac) == 50, "%d mapped macros, expected 50"
          % len(mapped_mac))
    check(len(external) == 6, "%d external names, expected 6" % len(external))
    check(len(rejected) == 1, "%d rejected names, expected 1" % len(rejected))
    check(data["sha256"] == gen.DEF_H_SHA256,
          "ledger sha256 does not match the frozen pin")
    for e in entries:
        check(e["disposition"] in ("mapped", "external", "rejected"),
              "entry %s has a fourth-bucket disposition %r"
              % (e["name"], e["disposition"]))
        check(e["line"] > 0, "entry %s has no DEF.H line number" % e["name"])
    print("ledger shape: 75 = 18 mapped typedefs + 50 mapped macros + "
          "6 external + 1 rejected")

    # ------------------------------------------------------------------
    # 2. Width ruling: the six 16-bit-width names map to short, everything
    #    mapped maps to the exact official type (or the recorded width
    #    deviation).  Independently restated here, not copied from the
    #    generator: Keil C251 int=16 vs this target int=32.
    # ------------------------------------------------------------------
    EXPECT_TYPES = {
        "BYTE": "unsigned char", "WORD": "unsigned short",
        "DWORD": "unsigned long", "CHAR": "signed char",
        "INT": "signed short", "LONG": "signed long",
        "uint8": "unsigned char", "uint16": "unsigned short",
        "uint32": "unsigned long", "int8": "signed char",
        "int16": "signed short", "int32": "signed long",
        "u8": "unsigned char", "u16": "unsigned short",
        "u32": "unsigned long", "s8": "signed char",
        "s16": "signed short", "s32": "signed long",
    }
    for name, want in EXPECT_TYPES.items():
        check(name in by_name and by_name[name]["expansion"] == want,
              "%s must map to %s (ledger: %r)"
              % (name, want, by_name.get(name, {}).get("expansion")))
    WIDTH_DEVIATIONS = {"WORD", "INT", "uint16", "int16", "u16", "s16"}
    for name in EXPECT_TYPES:
        official = by_name[name]["official"]
        ours = by_name[name]["expansion"]
        if name in WIDTH_DEVIATIONS:
            check(official != ours,
                  "%s width deviation must be recorded (official %s)"
                  % (name, official))
            check("16-bit" in by_name[name]["reason"],
                  "%s width deviation must state the 16-bit reason" % name)
        else:
            check(norm(official) == norm(ours),
                  "%s must map to the official type %s (got %s)"
                  % (name, official, ours))
    print("type mapping: 18 names exact, 6 deliberate 16-bit width "
          "deviations recorded with reasons")

    # ------------------------------------------------------------------
    # 3. Mapped macro fidelity: entries whose reason says "official
    #    exact"/"official value" must match the official body after
    #    whitespace normalization; guards are additive only.
    # ------------------------------------------------------------------
    for e in mapped_mac:
        official_body = e["official"]
        if e["name"] in gen.FUNCTION_LIKE_PARAMS:
            official_body = e["official"][e["official"].index(")") + 1:]
        if e["reason"].startswith("official exact") \
                or e["reason"].startswith("official value"):
            check(norm(official_body) == norm(e["expansion"]),
                  "macro %s body %r must equal the official %r"
                  % (e["name"], e["expansion"], official_body))
    print("macro fidelity: every 'official exact/value' body matches the "
          "official DEF.H after whitespace normalization")

    # ------------------------------------------------------------------
    # 4. Rejected and external behavior in the header text.
    # ------------------------------------------------------------------
    header = HEADER_PATH.read_text(encoding="utf-8")
    check("#define BOOL" in header and "GCC error" in header
          and "_Bool or unsigned char" in header,
          "BOOL must carry the precise reject pragma")
    check("typedef" not in header.split("Rejected")[1].split("External")[0],
          "the rejected section must not contain a typedef mapping")
    for name in ("uint8_t", "uint16_t", "uint32_t",
                 "int8_t", "int16_t", "int32_t"):
        check(re.search(r"#define\s+%s\b|\btypedef\b[^;]*\b%s\s*;" % (name, name),
                        header) is None,
              "external name %s must never be emitted" % name)
        check("// %s:" % name in header,
              "external name %s must be documented as a comment" % name)
    print("rejected/external: BOOL precise error, stdint names documented "
          "and never emitted")

    # ------------------------------------------------------------------
    # 5. --check-header accepts the in-tree header and rejects crafted bad
    #    headers (not vacuous).
    # ------------------------------------------------------------------
    run_generator(["--check-header", str(HEADER_PATH)])
    print("check-header: in-tree header accepted")

    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        # 5a. silent downgrade of BOOL to unsigned char
        bad1 = header.replace(
            '#define BOOL \\\n    _Pragma("GCC error \\"MCS251: BOOL maps',
            'typedef unsigned char BOOL; /*downgraded*/ #define __x \\\n'
            '    _Pragma("GCC error \\"MCS251: BOOL maps')
        bad1p = tdp / "bad1.h"
        bad1p.write_text(bad1, encoding="utf-8")
        check(run_generator(["--check-header", str(bad1p)],
                            expect_rc=1).returncode != 0,
              "crafted BOOL-downgrade header must fail --check-header")
        # 5b. sneaking in a uint8_t typedef
        bad2 = header.replace(
            "typedef unsigned char BYTE;",
            "typedef unsigned char BYTE;\ntypedef unsigned char uint8_t;",
            1)
        bad2p = tdp / "bad2.h"
        bad2p.write_text(bad2, encoding="utf-8")
        check(run_generator(["--check-header", str(bad2p)],
                            expect_rc=1).returncode != 0,
              "crafted uint8_t-emitting header must fail --check-header")
        # 5c. widening WORD back to unsigned int (undoing the width ruling)
        bad3 = header.replace("typedef unsigned short WORD;",
                              "typedef unsigned int WORD;", 1)
        bad3p = tdp / "bad3.h"
        bad3p.write_text(bad3, encoding="utf-8")
        check(run_generator(["--check-header", str(bad3p)],
                            expect_rc=1).returncode != 0,
              "crafted WORD-widening header must fail --check-header")
        # 5d. dropping a mapped macro
        bad4 = re.sub(r"#ifndef NULL\n#define NULL  0\n#endif\n", "",
                      header, count=1)
        bad4p = tdp / "bad4.h"
        bad4p.write_text(bad4, encoding="utf-8")
        check(run_generator(["--check-header", str(bad4p)],
                            expect_rc=1).returncode != 0,
              "crafted macro-dropping header must fail --check-header")
        # Byte identity includes line endings, not universal-newline text.
        crlf = tdp / "crlf.h"
        crlf.write_bytes(HEADER_PATH.read_bytes().replace(b"\n", b"\r\n"))
        run_generator(["--check-header", str(crlf)], expect_rc=1)
        # 5e. a tampered DEF.H must be rejected by the sha256 pin
        tampered = tdp / "DEF.H"
        tampered.write_text(
            OFFICIAL_DEF_H.read_text(encoding="utf-8",
                                     errors="replace")
            + "\ntypedef unsigned char EXTRA_NAME;\n",
            encoding="utf-8")
        out = tdp / "out.h"
        check(run_generator(["--def-h", str(tampered), "--out", str(out),
                             "--ledger", str(tdp / "l.json")],
                            expect_rc=1).returncode != 0,
              "tampered DEF.H must be rejected (sha256 pin / unaccounted "
              "name)")
        print("crafted bad inputs: 5/5 rejected (BOOL downgrade, uint8_t "
              "emission, WORD widening, macro drop, DEF.H tampering)")

    # ------------------------------------------------------------------
    # 6. Optional: compile-time behaviour with a real clang.
    # ------------------------------------------------------------------
    if args.clang:
        clang = str(args.clang)
        inc = str(ROOT / "include")

        def comp(src, name, extra=()):
            p = subprocess.run(
                [clang, "--target=mcs251-unknown-none", "-std=c11",
                 "-fmcs251-keil", "-I", inc, "-S", "-emit-llvm",
                 "-x", "c", "-o", "-", "-"] + list(extra),
                input=src, capture_output=True, text=True)
            return p

        # 6a. the header compiles clean and the anchors keep their widths.
        p = comp('#include "mcs251_type_compat.h"\n'
                 'BYTE bb = 1; WORD ww = MAKEWORD(0x34, 0x12);\n'
                 'DWORD dd = MAKELONG(1, 2);\n'
                 'u8 uu = BIT3 | PIN_5;\n'
                 'unsigned f(void){ SET_REG_BIT(uu, BIT7); return uu; }\n',
                 "clean")
        check(p.returncode == 0 and "error" not in p.stderr,
              "clean compile failed: %s" % p.stderr.strip()[:300])
        check("@ww = dso_local global i16 4660" in p.stdout,
              "WORD must be 16-bit and MAKEWORD must build 0x1234")
        check("@dd = dso_local global i32 131073" in p.stdout,
              "DWORD must be 32-bit and MAKELONG must build 0x00020001")
        print("clang behaviour: header compiles, WORD/DWORD widths and "
              "MAKEWORD/MAKELONG values verified at IR level")

        # 6b. BOOL is rejected with the precise message.
        p = comp('#include "mcs251_type_compat.h"\nBOOL flag;\n', "bool")
        check(p.returncode != 0
              and "BOOL maps to the Keil 'bit' type" in p.stderr,
              "BOOL must be rejected with the precise message")
        print("clang behaviour: BOOL rejected with the precise message")

        # 6c. the stdint-shaped names stay usable through <stdint.h>.
        p = comp('#include "mcs251_type_compat.h"\n#include <stdint.h>\n'
                 'uint8_t a = 1; uint16_t b = 2;\n'
                 'unsigned f(void){ return (unsigned)a + b; }\n', "stdint")
        check(p.returncode == 0 and "error" not in p.stderr,
              "uint8_t/uint16_t must compose with <stdint.h>: %s"
              % p.stderr.strip()[:300])
        p = comp('#include <stdint.h>\n#include "mcs251_type_compat.h"\n'
                 'uint8_t a; uint16_t b; uint32_t c;\n'
                 'int8_t d; int16_t e; int32_t f;\n', "stdint-first")
        check(p.returncode == 0 and "error" not in p.stderr,
              "all six stdint names must compose in reverse include order: %s"
              % p.stderr.strip()[:300])
        print("clang behaviour: external stdint names compose with "
              "<stdint.h> in both include orders")

    print()
    if failures:
        print("test-type-compat: FAIL (%d/%d checks failed)"
              % (len(failures), checks))
        return 1
    print("test-type-compat: PASS (%d checks)" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
