#!/usr/bin/env python3
"""Run the minimal porting sample and require its complete UART sentinel."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

SENTINEL = b"PORTING-MINIMAL-PASS\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--serial", required=True, type=Path)
    args = parser.parse_args()

    args.serial.parent.mkdir(parents=True, exist_ok=True)
    command = [
        args.qemu,
        "-M",
        "stc32g144k246",
        "-bios",
        str(args.image),
        "-accel",
        "tcg",
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
    ]
    try:
        completed = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True, timeout=15)
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or b""
        stderr = exc.stderr or b""
        args.serial.write_bytes(stdout)
        args.serial.with_suffix(".stderr").write_bytes(stderr)
        if stdout == SENTINEL:
            print(SENTINEL.decode(), end="")
            return 0
        print("minimal sample: UART sentinel missing before timeout", file=sys.stderr)
        return 1

    args.serial.write_bytes(completed.stdout)
    args.serial.with_suffix(".stderr").write_bytes(completed.stderr)
    if completed.returncode == 0 and completed.stdout == SENTINEL:
        print(SENTINEL.decode(), end="")
        return 0
    print(
        f"minimal sample: QEMU rc={completed.returncode}, UART={completed.stdout!r}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
