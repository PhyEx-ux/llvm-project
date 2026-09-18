#!/usr/bin/env python3
"""Run a command and assert its EXACT exit status.

`not <cmd>` (lit's helper) only requires a NON-ZERO status.  The G11-D
contract (section 1 / section 5) requires the exact code: 0 on success and 1
for every failure, with no partial-success code.  This wrapper forwards the
child's stdout and stderr (merged, in that order) and exits 0 only when the
child's status equals the expected one, so `... | FileCheck` still sees the
child's real output.

Usage: expect-rc.py <expected-rc> <command> [args...]
"""
import subprocess
import sys

want = int(sys.argv[1])
p = subprocess.run(sys.argv[2:], capture_output=True)
sys.stdout.buffer.write(p.stdout)
sys.stdout.buffer.write(p.stderr)
sys.stdout.flush()
sys.exit(0 if p.returncode == want else 1)
