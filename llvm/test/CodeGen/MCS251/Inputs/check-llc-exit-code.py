#!/usr/bin/env python3
"""WP4 round 4: assert the EXACT exit status of llc's capability rejections.

`not llc ... | FileCheck` proves "nonzero and the expected text": it accepts a
crash (134), an abort, or exit 70 just as readily as the intended 1. This
helper pins the status itself. Every rejected snippet must exit with exactly 1
and print the unified contract diagnostic with no crash text; the positive
control must exit 0.

Usage: check-llc-exit-code.py --llc <path-to-llc>
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HEADER = 'target triple = "mcs251-unknown-none"\n'
SECTION = ";--- %s\n"

# (name, IR body) -- each is a rejected construct; the section name matches the
# shape exercised in unsupported-capabilities.ll.
REJECTED = (
    (
        "atomic-load",
        "define i8 @f(ptr %p) {\n"
        "  %v = load atomic i8, ptr %p seq_cst, align 1\n"
        "  ret i8 %v\n"
        "}\n",
    ),
    (
        "weak-def",
        "define weak i16 @w(i16 %a) {\n"
        "  ret i16 %a\n"
        "}\n",
    ),
    (
        "module-asm",
        'module asm "nop"\n',
    ),
    (
        "computed-goto-addr",
        "@tab = global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) "
        "blockaddress(@f, %b) to ptr)]\n"
        "define void @f() {\n"
        "entry:\n"
        "  br label %b\n"
        "b:\n"
        "  ret void\n"
        "}\n",
    ),
)

# A supported module: volatile access is the documented alternative.
POSITIVE = (
    "atomic-alternative",
    "@shared = global i16 0\n"
    "define i16 @g() {\n"
    "  %v = load volatile i16, ptr @shared, align 2\n"
    "  ret i16 %v\n"
    "}\n",
)

CRASH_TEXT = (
    "PLEASE submit",
    "Stack dump",
    "PLEASE ATTACH",
    "crash backtrace",
)

failures = []


def run_llc(llc, path, out):
    return subprocess.run(
        [
            llc,
            "-mtriple=mcs251",
            "-mcs251-memory-contract=1,2,32,8,1",
            "-filetype=null",
            "-O0",
            str(path),
            "-o",
            str(out),
        ],
        capture_output=True,
        text=True,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--llc", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for name, body in REJECTED:
            src = tmp / (name + ".ll")
            src.write_text(HEADER + body)
            result = run_llc(args.llc, src, tmp / (name + ".out"))
            if result.returncode != 1:
                failures.append(
                    f"{name}: expected exit status 1, got {result.returncode}"
                )
            combined = result.stdout + result.stderr
            if "MCS251 contract violation" not in combined:
                failures.append(
                    f"{name}: missing contract diagnostic: {combined[:300]}"
                )
            for needle in CRASH_TEXT:
                if needle in combined:
                    failures.append(f"{name}: crash text {needle!r} present")

        name, body = POSITIVE
        src = tmp / (name + ".ll")
        src.write_text(HEADER + body)
        result = run_llc(args.llc, src, tmp / (name + ".out"))
        if result.returncode != 0:
            failures.append(
                f"positive control {name}: expected status 0, got "
                f"{result.returncode}: {(result.stdout + result.stderr)[:300]}"
            )

    if failures:
        print("FAIL")
        for line in failures:
            print("  " + line)
        return 1
    print(
        f"llc exit status is exactly 1 for {len(REJECTED)} rejections; "
        "positive control exits 0"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
