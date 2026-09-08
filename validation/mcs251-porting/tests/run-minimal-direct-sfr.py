#!/usr/bin/env python3
"""Build and QEMU-run the smallest direct-SFR + <intrins.h> porting sample."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


EXPECTED_UART = b"PORTING-MINIMAL-PASS\n"


def run(step: str, command: list[str], cwd: Path) -> dict[str, object]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, encoding="utf-8", timeout=120)
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
    parser.add_argument("--output", required=True, type=Path, help="write result JSON")
    parser.add_argument("--readobj", default="/home/liu/mcs251-demo-test/bin-frozen/llvm-readobj")
    args = parser.parse_args()

    root = args.root.resolve()
    sample = root / "examples" / "minimal-direct-sfr"
    steps = [
        run("make-clean-all", ["make", "--no-print-directory", "clean", "all"], sample),
        run("make-run-qemu", ["make", "--no-print-directory", "run"], sample),
    ]
    build = sample / "build"
    serial = build / "minimal.serial"
    if serial.read_bytes() != EXPECTED_UART:
        raise RuntimeError(f"unexpected UART transcript: {serial.read_bytes()!r}")

    helper = build / "elf-nop.o"
    inspected = subprocess.run(
        [args.readobj, "--sections", "--section-data", "--symbols", str(helper)],
        text=True,
        capture_output=True,
        encoding="utf-8",
    )
    if inspected.returncode:
        raise RuntimeError(f"cannot inspect ELF NOP helper: {inspected.stderr.strip()}")
    if "0000: 00AA" not in inspected.stdout or "_mcs251_porting_elf_nop" not in inspected.stdout:
        raise RuntimeError("ELF NOP helper does not contain NOP; ERET or its C ABI symbol")

    output = {
        "format": "mcs251-porting-minimal-direct-sfr-v1",
        "passed": True,
        "source": "examples/minimal-direct-sfr/main.c",
        "uses": {
            "direct_sfr_macros": ["P0", "P1", "P4", "SBUF"],
            "intrinsic": "_nop_()",
            "unsupported_keil_features": [],
        },
        "uart_expected": EXPECTED_UART.decode("ascii"),
        "uart_actual": serial.read_text(encoding="ascii"),
        "object_magic": (build / "main.o").read_bytes()[:4].hex(),
        "elf_magic": (build / "minimal.elf").read_bytes()[:4].hex(),
        "hex_starts_with_colon": (build / "minimal.hex").read_bytes().startswith(b":"),
        "elf_nop_helper": {
            "object": str(helper),
            "machine_bytes": "00 AA",
            "symbol": "_mcs251_porting_elf_nop",
            "readobj": inspected.stdout,
        },
        "steps": steps,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("minimal-direct-sfr: ELF chain and QEMU UART PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"run-minimal-direct-sfr: error: {exc}", file=sys.stderr)
        sys.exit(1)
