#!/usr/bin/env python3
"""BT06 self-tests for the bit compat header and its generator.

Runs without a compiler for the data/consistency checks, and (optionally) with
a provided clang for the compile-time behaviour checks.

Every ``check()`` below is a real assertion: it inspects actual return codes,
stderr text, or parsed artifacts, and any failure makes this script exit
non-zero.  The old always-true loop over rejected names was removed (BT06-3).

Usage:
    python3 tests/test-bit-compat.py                 # data checks only
    python3 tests/test-bit-compat.py --clang <path>  # + compile behaviour
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JSON_PATH = ROOT / "bit-registers.json"
HEADER_PATH = ROOT / "include" / "mcs251_bit_compat.h"
GEN_REG = ROOT / "tools" / "gen-bit-registers.py"
GEN_HDR = ROOT / "tools" / "gen-bit-compat-header.py"
OFFICIAL_HEADER = Path("/home/liu/LLVM_STC32/STC32G144K246-DEMO-CODE/COMM/STC32G144K246.H")

# Import the generators as modules so the test exercises the very same header
# parser/checker that --check-header uses and the very same reject-class
# mapping that renders the header (the filenames have dashes, so plain imports
# do not work).
def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gen_bit_registers = _load_module(GEN_REG, "gen_bit_registers")
gen_bit_compat_header = _load_module(GEN_HDR, "gen_bit_compat_header")

# BT06-1 reclassification: the 35 official sbits that used to sit in the
# deleted "unsupported" / not_in_first_slice_pending_pm bucket.  Peripheral
# control bits over bit-addressable bases; the coordinator ruling converts
# them all.  Addresses re-derived independently from the official header
# (base SFR address + bit index), not copied from the generator.
BT06_RECLASSIFIED = {
    # TCON (0x88)
    "IT0": 0x88, "IE0": 0x89, "IT1": 0x8A, "IE1": 0x8B, "TR0": 0x8C, "TR1": 0x8E,
    # SCON (0x98); 0x98/0x99 are RI/TI (first slice)
    "RB8": 0x9A, "TB8": 0x9B, "REN": 0x9C, "SM2": 0x9D, "SM1": 0x9E, "SM0": 0x9F,
    # IE (0xA8); 0xAF is EA and stays restricted
    "EX0": 0xA8, "ET0": 0xA9, "EX1": 0xAA, "ET1": 0xAB, "ES": 0xAC,
    "EADC": 0xAD, "ELVD": 0xAE,
    # P3 (0xB0) alternate-function aliases of P30..P37
    "RXD": 0xB0, "TXD": 0xB1, "INT0": 0xB2, "INT1": 0xB3,
    "T0": 0xB4, "T1": 0xB5, "WR": 0xB6, "RD": 0xB7,
    # IP (0xB8)
    "PX0": 0xB8, "PT0": 0xB9, "PX1": 0xBA, "PT1": 0xBB, "PS": 0xBC,
    "PADC": 0xBD, "PLVD": 0xBE, "PPCA": 0xBF,
}
# The eight P3 aliases must map to the same bit addresses as P30..P37.
P3_ALIAS_OF = {
    "RXD": "P30", "TXD": "P31", "INT0": "P32", "INT1": "P33",
    "T0": "P34", "T1": "P35", "WR": "P36", "RD": "P37",
}

failures: list[str] = []
checks = 0


def check(cond: bool, msg: str) -> None:
    global checks
    checks += 1
    if not cond:
        failures.append(msg)


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def test_data_and_consistency() -> None:
    doc = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    header = HEADER_PATH.read_text(encoding="utf-8")

    check(doc["format"] == "mcs251-bit-registers-v1", "unexpected JSON format tag")
    check(doc["counts"]["total"] == len(doc["records"]), "counts.total != len(records)")

    approved = [r for r in doc["records"] if r["status"] == "approved_pending_pm"]
    restricted = [r for r in doc["records"] if r["status"] == "restricted"]
    non_bit_addr = [r for r in doc["records"] if r["status"] == "rejected_non_bit_addressable"]
    check(len(approved) == 103, f"expected 103 approved (68 first slice + 35 reclassified), got {len(approved)}")
    check(len(restricted) == 30, f"expected 30 restricted, got {len(restricted)}")
    check(
        len(non_bit_addr) == 215,
        f"expected 215 rejected_non_bit_addressable (210 official + 5 report-only), got {len(non_bit_addr)}",
    )
    # BT06-1: the unsupported / not_in_first_slice_pending_pm bucket must not
    # exist anywhere any more -- neither as records nor as a legend entry.
    check(
        all(r["status"] != "unsupported" for r in doc["records"]),
        "the 'unsupported' bucket must not reappear as a record status",
    )
    check("unsupported" not in doc["counts"]["by_status"], "'unsupported' must not appear in counts.by_status")
    check("unsupported" not in doc["status_legend"], "'unsupported' must not appear in status_legend")

    # BT06-1 total accounting, written out in full: the official header
    # declares 343 sbits = 103 converted + 30 restricted + 210 non-bit-
    # addressable; the JSON additionally carries 5 report-only records
    # (348 = 103 + 30 + 215), each still annotated with its non-official
    # source.
    check(doc["counts"]["total"] == 348, f"expected 348 JSON records, got {doc['counts']['total']}")
    check(103 + 30 + 215 == 348, "JSON ledger 348 = 103 + 30 + 215 must hold")

    # Documented special cases.
    by_name = {r["name"]: r for r in doc["records"]}
    check(by_name["CY"]["bitaddr"] == 0xD7, "CY must be 0xD7 (PSW.CY)")
    # bit 0xFF backs onto byte 0xF8, not the direct byte at 0xFF.
    ff = next((r for r in doc["records"] if r["bitaddr"] == 0xFF), None)
    if ff is not None:
        check(ff["backing_byte"] == 0xF8, "bit 0xFF must back onto byte 0xF8")

    # Approved bit addresses must be in the SFR bit space and consistent.
    for r in approved:
        check(128 <= r["bitaddr"] <= 255, f"{r['name']} bitaddr out of SFR space")
        check(r["backing_byte"] == (r["bitaddr"] & 0xF8), f"{r['name']} backing byte wrong")
        check(r["bit_index"] == (r["bitaddr"] & 7), f"{r['name']} bit index wrong")

    # Non-bit-addressable records must carry an explicit reason and no address.
    for r in non_bit_addr:
        check(
            r["bitaddr"] is None,
            f"{r['name']} is rejected_non_bit_addressable but has bitaddr {r['bitaddr']}",
        )
        check(
            r["reason"] is not None and "non_bit_addressable_base" in r["reason"],
            f"{r['name']} rejection reason missing",
        )

    # BT06-1: full official coverage.  Every sbit name declared by the
    # official Keil header must have an explicit JSON record (converted or
    # rejected with a reason); nothing may be silently dropped.
    if OFFICIAL_HEADER.is_file():
        parsed = gen_bit_registers.parse_official_header(OFFICIAL_HEADER)
        official_names = list(dict.fromkeys(e["name"] for e in parsed["sbit"]))
        missing = [n for n in official_names if n not in by_name]
        check(not missing, f"official sbit names missing from JSON: {missing}")
        cov = doc["official_coverage"]
        check(cov["official_sbit_count"] == len(official_names), "coverage count != official sbit count")
        check(cov["recorded"] == len(official_names), "coverage.recorded != official sbit count")
        check(
            cov["converted"] + cov["explicitly_rejected"] == len(official_names),
            "converted + rejected != official total",
        )
        check(cov["converted"] == len(approved), "converted != approved record count")
        check(
            cov["by_bucket"]["rejected_non_bit_addressable"]
            == sum(1 for n in official_names if by_name[n]["status"] == "rejected_non_bit_addressable"),
            "coverage nba bucket disagrees with records",
        )
        # BT06-1 ledger, fixed: 343 official sbits = 103 + 30 + 210, and the
        # deleted not-in-first-slice bucket must stay deleted.
        check(cov["official_sbit_count"] == 343, "official sbit total must be 343")
        check(cov["converted"] == 103, "coverage converted must be 103")
        check(cov["by_bucket"]["rejected_restricted"] == 30, "coverage restricted must be 30")
        check(cov["by_bucket"]["rejected_non_bit_addressable"] == 210, "coverage nba must be 210")
        check(103 + 30 + 210 == 343, "official ledger 343 = 103 + 30 + 210 must hold")
        check(
            "recorded_not_in_first_slice" not in cov["by_bucket"],
            "coverage must not carry a not-in-first-slice bucket any more",
        )
    else:
        print(f"SKIP: official header {OFFICIAL_HEADER} not present; coverage check not run")

    # BT06-3(a): real per-name assertions against the actual header text.
    # The header mappings are parsed with the same code path as --check-header,
    # so these assertions fail if any non-approved name is mapped to the
    # builtin, or any approved/rejected mapping is missing or wrong.
    header_bits, header_rejects, parse_problems = gen_bit_registers.classify_header_defs(header)
    check(not parse_problems, f"header has shadowing/redefinition problems: {parse_problems}")
    check(len(header_bits) == len(approved), f"header maps {len(header_bits)} names, expected {len(approved)}")

    for r in approved:
        name = r["name"]
        check(
            header_bits.get(name) == r["bitaddr"],
            f"approved {name}: header ICE "
            f"{hex(header_bits[name]) if name in header_bits else 'MISSING'} != JSON {hex(r['bitaddr'])}",
        )
    for r in restricted + non_bit_addr:
        name = r["name"]
        check(name not in header_bits, f"{r['status']} name {name} must not be builtin-mapped")
        check(
            name in header_rejects and header_rejects[name] == f"MCS251_error_{gen_bit_compat_header.reject_class(r['reason'] or '')}",
            f"{r['status']} name {name} missing/wrong explicit reject macro",
        )

    # BT06-1: the 35 reclassified names, asserted one by one against the
    # independently derived addresses: JSON status, JSON bitaddr, and the
    # header's builtin mapping must all carry exactly that address.
    check(
        set(BT06_RECLASSIFIED) <= {r["name"] for r in approved},
        "every BT06-1 reclassified name must be an approved record",
    )
    for name, want in BT06_RECLASSIFIED.items():
        r = by_name.get(name)
        check(r is not None, f"reclassified {name} missing from JSON")
        if r is None:
            continue
        check(
            r["status"] == "approved_pending_pm",
            f"reclassified {name} status is {r['status']}, expected approved_pending_pm",
        )
        check(r["bitaddr"] == want, f"reclassified {name} bitaddr {r['bitaddr']!r} != {hex(want)}")
        check(
            header_bits.get(name) == want,
            f"reclassified {name}: header ICE "
            f"{hex(header_bits[name]) if name in header_bits else 'MISSING'} != {hex(want)}",
        )
    # The P3 alternate-function names are aliases: identical bit addresses to
    # the already-supported P30..P37 macros.
    for alias, port in P3_ALIAS_OF.items():
        check(
            by_name[alias]["bitaddr"] == by_name[port]["bitaddr"],
            f"P3 alias {alias} must share the bit address of {port}",
        )
    unexpected_maps = set(header_bits) - {r["name"] for r in approved}
    check(not unexpected_maps, f"header builtin-maps names absent from approved set: {sorted(unexpected_maps)}")
    # Nothing outside the record set may carry a reject macro either.
    unexpected_rejects = set(header_rejects) - {r["name"] for r in restricted + non_bit_addr}
    check(not unexpected_rejects, f"header reject-maps names absent from rejected set: {sorted(unexpected_rejects)}")

    # The generator must reproduce the checked-in header byte for byte.
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "mcs251_bit_compat.h"
        cp = run([sys.executable, str(GEN_HDR), "-o", str(out)])
        check(cp.returncode == 0, f"gen-bit-compat-header.py failed: {cp.stderr}")
        if cp.returncode == 0:
            check(out.read_text(encoding="utf-8") == header, "regenerated header differs from checked-in header")

    # check_header must pass on the real header.
    cp = run([sys.executable, str(GEN_REG), "--check-header", str(HEADER_PATH)])
    check(cp.returncode == 0, f"check-header failed: {cp.stderr}")

    # BT06-3(b): --check-header must reject adversarial headers.  Each mutant
    # is built the way Alice built hers (header-extra.py): a copy of the real
    # header with one targeted mutation.  The checker must exit non-zero and
    # name the offending line.
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)

        # Mutant 1 (Alice's): #undef EA, then map EA to a paren-wrapped
        # builtin at EA's SFR bit address 0xAF.
        m1 = td / "mutant-ea.h"
        m1.write_text(
            header + "\n#undef EA\n#define EA (__builtin_mcs251_bit_lvalue(0xAF))\n",
            encoding="utf-8",
        )
        cp = run([sys.executable, str(GEN_REG), "--check-header", str(m1)])
        check(cp.returncode != 0, "mutant EA-redefinition header must be rejected")
        check("EA" in cp.stderr, f"EA mutant diagnostic must name EA: {cp.stderr}")
        check("#undef EA" in cp.stderr, f"EA mutant diagnostic must name the #undef line: {cp.stderr}")

        # Mutant 2: an approved name paren-wrapped (non-canonical builtin
        # spelling), address otherwise correct.
        m2 = td / "mutant-paren.h"
        m2.write_text(
            header.replace(
                "#define P00    __builtin_mcs251_bit_lvalue(0x80)",
                "#define P00    (__builtin_mcs251_bit_lvalue(0x80))",
            ),
            encoding="utf-8",
        )
        cp = run([sys.executable, str(GEN_REG), "--check-header", str(m2)])
        check(cp.returncode != 0, "mutant paren-wrapped P00 header must be rejected")
        check("P00" in cp.stderr, f"P00 mutant diagnostic must name P00: {cp.stderr}")

        # Mutant 3: an unknown name silently mapped to the builtin.
        m3 = td / "mutant-unknown.h"
        m3.write_text(header + "\n#define FOO (__builtin_mcs251_bit_lvalue(0x80))\n", encoding="utf-8")
        cp = run([sys.executable, str(GEN_REG), "--check-header", str(m3)])
        check(cp.returncode != 0, "mutant unknown-name builtin mapping must be rejected")
        check("FOO" in cp.stderr, f"FOO mutant diagnostic must name FOO: {cp.stderr}")

        # Mutant 4: restricted macro swapped for a plain builtin (no #undef).
        m4 = td / "mutant-direct-ea.h"
        m4.write_text(
            header.replace(
                "#define EA     MCS251_error_compiler_managed",
                "#define EA     __builtin_mcs251_bit_lvalue(0xAF)",
            ),
            encoding="utf-8",
        )
        cp = run([sys.executable, str(GEN_REG), "--check-header", str(m4)])
        check(cp.returncode != 0, "mutant direct EA builtin mapping must be rejected")
        check("EA" in cp.stderr, f"direct-EA mutant diagnostic must name EA: {cp.stderr}")


def compile_snippet(clang: str, src: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "t.c"
        f.write_text(src, encoding="utf-8")
        return run([
            clang, "-cc1", "-triple", "mcs251-unknown-none", "-std=c11",
            "-I", str(ROOT / "include"), "-fsyntax-only", str(f),
        ])


def compile_keil_sbit(clang: str, src: str) -> subprocess.CompletedProcess:
    """Compile a Keil-dialect snippet (sbit declarations) through the driver
    layer: --target=mcs251-unknown-none -fmcs251-keil."""
    with tempfile.TemporaryDirectory() as td:
        f = Path(td) / "t.c"
        f.write_text(src, encoding="utf-8")
        return run([
            clang, "--target=mcs251-unknown-none", "-fmcs251-keil", "-std=c11",
            "-fsyntax-only", str(f),
        ])


def test_compile_behaviour(clang: str) -> None:
    ok = compile_snippet(clang, '#include "mcs251_bit_compat.h"\n'
                                'void f(void){ P00 = 1; P33 = 0; if (P10) {} TF0 = 1; RI = 1; }\n')
    check(ok.returncode == 0, f"approved bit names should compile: {ok.stderr}")

    # The header itself must stay syntactically clean.
    ok = compile_snippet(clang, '#include "mcs251_bit_compat.h"\n')
    check(ok.returncode == 0, f"bare include should compile: {ok.stderr}")

    # BT06-2: the MCS251_RAM_BIT 0..127 contract is enforced at compile time.
    for good in (0, 1, 127):
        cp = compile_snippet(clang, '#include "mcs251_bit_compat.h"\n'
                                     f'void f(void){{ MCS251_RAM_BIT({good}) = 1; }}\n')
        check(cp.returncode == 0, f"MCS251_RAM_BIT({good}) must compile: {cp.stderr}")
    for bad in (128, 0xAF, 0x100, -1):
        cp = compile_snippet(clang, '#include "mcs251_bit_compat.h"\n'
                                     f'void f(void){{ MCS251_RAM_BIT({bad}) = 1; }}\n')
        check(cp.returncode != 0, f"MCS251_RAM_BIT({bad}) must be rejected")
        check(
            "array size is negative" in cp.stderr,
            f"MCS251_RAM_BIT({bad}) must fail with the range-guard diagnostic: {cp.stderr}",
        )

    for name, expected in [("EA", "compiler-managed"), ("CY", "compiler-managed"), ("EAXFR", "XFR-enable")]:
        cp = compile_snippet(clang, f'#include "mcs251_bit_compat.h"\nvoid f(void){{ {name} = 1; }}\n')
        check(cp.returncode != 0, f"restricted {name} must not compile")
        check(expected in cp.stderr, f"restricted {name} diagnostic missing '{expected}': {cp.stderr}")

    # BT06-1: an official name with a non-bit-addressable base is answered
    # with an explicit hard error, not silence.
    for name in ("SMOD", "CANEDIN", "PPWME"):
        cp = compile_snippet(clang, f'#include "mcs251_bit_compat.h"\nvoid f(void){{ {name} = 1; }}\n')
        check(cp.returncode != 0, f"non-bit-addressable {name} must not compile")
        check(
            "not bit-addressable" in cp.stderr,
            f"non-bit-addressable {name} diagnostic missing reason: {cp.stderr}",
        )

    # BT06-1: the 35 reclassified names must be usable one by one, read and
    # written inside a function body, through the compat-header macros.
    for name in BT06_RECLASSIFIED:
        cp = compile_snippet(
            clang,
            f'#include "mcs251_bit_compat.h"\nvoid f(void){{ {name} = 1; if ({name}) {{}} }}\n',
        )
        check(cp.returncode == 0, f"reclassified {name} read/write must compile: {cp.stderr}")

    # BT06-1: the same names are also expressible directly as Keil `sbit`
    # declarations.  Representatives of every reclassified SFR family
    # (TCON/SCON/IE/IP/P3) are checked in both spellings: the direct
    # bit-address form `sbit NAME = 0xNN;` and the base^bit positioning form
    # `sbit NAME = BASE^n;` (the base is spelled as a constant macro here; the
    # `sfr` keyword itself is not a declaration this frontend accepts).
    for name, base, base_addr, bit in (
        ("TR0", "TCON", 0x88, 4), ("REN", "SCON", 0x98, 4),
        ("EX0", "IE", 0xA8, 0), ("PS", "IP", 0xB8, 4), ("RD", "P3", 0xB0, 7),
    ):
        want = BT06_RECLASSIFIED[name]
        direct = compile_keil_sbit(
            clang, f"sbit {name} = 0x{want:02X};\nvoid f(void){{ {name} = 1; if ({name}) {{}} }}\n")
        check(direct.returncode == 0, f"sbit {name} = 0x{want:02X}; (direct form) must compile: {direct.stderr}")
        positioned = compile_keil_sbit(
            clang,
            f"#define {base} 0x{base_addr:02X}\n"
            f"sbit {name} = {base}^{bit};\n"
            f"void f(void){{ {name} = 1; if ({name}) {{}} }}\n",
        )
        check(
            positioned.returncode == 0,
            f"sbit {name} = {base}^{bit}; (base^bit form) must compile: {positioned.stderr}",
        )

    # A name with no JSON record at all stays undeclared and fails loudly.
    cp = compile_snippet(clang, '#include "mcs251_bit_compat.h"\nvoid f(void){ MCS251_NO_SUCH_BIT = 1; }\n')
    check(cp.returncode != 0, "a name absent from the JSON must not compile")
    check("undeclared" in cp.stderr, f"absent name should be undeclared: {cp.stderr}")

    # Regression: every approved name usable together (Alice's header-all form).
    doc = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    approved = [r["name"] for r in doc["records"] if r["status"] == "approved_pending_pm"]
    src = '#include "mcs251_bit_compat.h"\nvoid f(void){ ' + "".join(n + "=1;" for n in approved) + " }\n"
    cp = compile_snippet(clang, src)
    check(cp.returncode == 0, f"all 103 approved names must compile together: {cp.stderr}")

    # Regression: every restricted name hard-errors individually.
    for r in doc["records"]:
        if r["status"] != "restricted":
            continue
        name = r["name"]
        cp = compile_snippet(clang, f'#include "mcs251_bit_compat.h"\nvoid f(void){{ {name} = 1; }}\n')
        check(cp.returncode != 0, f"restricted {name} must not compile (rc was 0)")
        check("MCS251:" in cp.stderr, f"restricted {name} must fail with an MCS251: diagnostic: {cp.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clang", default=None, help="clang binary for behaviour checks")
    args = parser.parse_args()

    test_data_and_consistency()
    if args.clang:
        test_compile_behaviour(args.clang)
    else:
        print("NOTE: run with --clang <path> to also exercise compile-time behaviour")

    if failures:
        print(f"FAIL: {len(failures)}/{checks} checks failed", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"PASS: {checks} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
