#!/usr/bin/env python3
"""运行 demo 固件，保留原始串口并拒绝超时、截断、同错伪通过。"""
import argparse
import difflib
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time


def require_pass(data, label):
    lines = data.splitlines()
    if not lines or lines[-1] != b"DEMO-PASS" or not data.endswith(b"\n"):
        raise RuntimeError(f"{label}: 缺少完整 DEMO-PASS 终止行")
    if len(lines) != 15 or any(not line.endswith(b":OK") for line in lines[:-1]):
        raise RuntimeError(f"{label}: 必须完整输出 14 项 OK，不能仅凭两侧相等判定")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--expected", type=Path)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    expected = args.expected.read_bytes() if args.expected else None
    if expected is not None:
        require_pass(expected, "宿主机")
    args.serial.parent.mkdir(parents=True, exist_ok=True)
    command = [args.qemu, "-M", args.machine, "-bios", str(args.image),
               "-accel", "tcg", "-icount", "shift=0,align=off,sleep=off",
               "-display", "none", "-monitor", "none", "-serial", "stdio"]
    output = bytearray()
    with args.serial.open("wb") as serial, args.serial.with_suffix(".stderr").open("wb") as err:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=err)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        deadline = time.monotonic() + args.timeout
        try:
            while time.monotonic() < deadline:
                if not selector.select(max(0, deadline - time.monotonic())):
                    break
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    break
                output.extend(chunk)
                serial.write(chunk)
                serial.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                if any(line == b"DEMO-PASS" or line.startswith(b"DEMO-FAIL(")
                       for line in output.split(b"\n")[:-1]):
                    break
        finally:
            selector.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdout.close()
    actual = bytes(output)
    require_pass(actual, "QEMU")
    if expected is not None:
        if actual != expected:
            sys.stderr.writelines(difflib.unified_diff(
                expected.decode(errors="replace").splitlines(True),
                actual.decode(errors="replace").splitlines(True),
                fromfile="host.expected", tofile="demo.serial"))
            raise RuntimeError("DEMO-CHECK-FAIL（输出不一致）")
        print("DEMO-CHECK-PASS（目标机与宿主机语义一致）")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
