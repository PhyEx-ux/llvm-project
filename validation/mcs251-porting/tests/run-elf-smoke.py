#!/usr/bin/env python3
"""Run the minimum clang -> ELF llc -> lld -> Intel-HEX production chain."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


LAYOUT = [
    "--area-start=HOME=0xff0000",
    "--area-start=VECS=0xff0003",
    "--area-start=BOOT=0xff0100",
    "--area-start=CSEG=0xff0200",
    "--area-start=XINIT=0xff8000",
]


def run(step: str, command: list[str]) -> dict[str, object]:
    result = subprocess.run(command, text=True, capture_output=True, encoding="utf-8")
    record = {
        "step": step,
        "command": command,
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    if result.returncode:
        raise RuntimeError(f"{step} failed: {result.stderr.strip()}")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="validation/mcs251-porting directory")
    parser.add_argument("--work", required=True, type=Path, help="directory for smoke artifacts")
    parser.add_argument("--output", required=True, type=Path, help="write result JSON")
    parser.add_argument("--clang", default="/home/liu/build-clang/bin/clang")
    parser.add_argument("--llc", default="/home/liu/mcs251-demo-test/bin-frozen/llc")
    parser.add_argument("--lld", default="/home/liu/build-mcs251-lld/bin/lld")
    parser.add_argument("--objcopy", default="/home/liu/mcs251-demo-test/bin-frozen/llvm-objcopy")
    args = parser.parse_args()

    root = args.root.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ll = work / "toolchain-smoke.ll"
    obj = work / "toolchain-smoke.o"
    elf = work / "toolchain-smoke.elf"
    ihex = work / "toolchain-smoke.hex"
    source = root / "tests" / "toolchain-smoke.c"

    steps = [
        run("clang", [args.clang, "--target=mcs251-unknown-none", "-std=c11", "-Wall", "-Wextra", "-Werror", "-S", "-emit-llvm", str(source), "-o", str(ll)]),
        run("llc-elf", [args.llc, "-mtriple=mcs251-unknown-none", "-verify-machineinstrs", "-mcs251-object-format=elf", "-filetype=obj", str(ll), "-o", str(obj)]),
        run("lld", [args.lld, "-flavor", "mcs251", "--edata-end", "0x0fff", *LAYOUT, "-o", str(elf), str(obj)]),
        run("objcopy", [args.objcopy, "-O", "ihex", str(elf), str(ihex)]),
    ]
    if obj.read_bytes()[:4] != b"\x7fELF":
        raise RuntimeError("llc output is not ELF; required flag was ignored")
    if elf.read_bytes()[:4] != b"\x7fELF":
        raise RuntimeError("lld output is not ELF")
    if not ihex.read_bytes().startswith(b":"):
        raise RuntimeError("objcopy output is not Intel HEX")

    output = {
        "format": "mcs251-porting-production-chain-smoke-v2",
        "passed": True,
        "object_magic": obj.read_bytes()[:4].hex(),
        "elf_magic": elf.read_bytes()[:4].hex(),
        "hex_starts_with_colon": True,
        "outputs": {path.name: path.stat().st_size for path in (ll, obj, elf, ihex)},
        "steps": steps,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print("elf-smoke: clang -> llc(ELF) -> lld -> objcopy PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"run-elf-smoke: error: {exc}", file=sys.stderr)
        sys.exit(1)
