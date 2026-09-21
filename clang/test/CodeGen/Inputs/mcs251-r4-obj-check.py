#!/usr/bin/env python3
"""WP4 round 4: check the accepted objects really are ELF, then re-run the
rejected counterparts and require an EXACT status of 1, no artifact, and a
clean stderr.

The accepted side is what the two cc1 RUN lines produced: the compiler must
have written a real ELF object for each of the four shapes at -O0 and -O2. The
rejected side is the same four shapes with the operand made genuinely
evaluated; `not` would only prove "nonzero", so the status is asserted here
instead.

Usage: mcs251-r4-obj-check.py <obj>... -- <clang-cc1 argv...>
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ELF_MAGIC = b"\x7fELF"


def expect_elf(path):
    try:
        blob = Path(path).read_bytes()
    except FileNotFoundError:
        # A RUN line that produced no object at all: fail with a message rather
        # than a traceback, so the lit output names the shape that broke.
        sys.exit(f"FAIL: no object was produced at {path}")
    if blob[:4] != ELF_MAGIC:
        sys.exit(f"FAIL: {path} is not an ELF object (magic {blob[:4].hex()})")
    if len(blob) <= 64:
        sys.exit(f"FAIL: {path} is too small to be an object ({len(blob)} bytes)")
    return len(blob)


if "--" not in sys.argv:
    sys.exit("usage: mcs251-r4-obj-check.py <obj>... -- <clang-cc1 argv...>")
split = sys.argv.index("--")
objects = sys.argv[1:split]
cc1 = sys.argv[split + 1:]
if not objects or not cc1:
    sys.exit("usage: mcs251-r4-obj-check.py <obj>... -- <clang-cc1 argv...>")

REJECTED = {
    "gnu-cond-common": """
int f(int x) { return (__atomic_signal_fence(0), x) ?: 7; }
""",
    "offsetof-index": """
struct of_target { int a[8]; };
int f(int i) {
  return __builtin_offsetof(struct of_target, a[(__atomic_signal_fence(0), i)]);
}
""",
    "stmt-expr-live": """
int f(void) { return ({ __atomic_signal_fence(0); 0; }); }
""",
    "a2-comma-rhs": """
int *p = ((int *)0, (int *)0xFF00);
""",
}

CRASH_TEXT = (
    "PLEASE submit",
    "Stack dump",
    "PLEASE ATTACH",
    "crash backtrace",
    "frontend command failed",
)

failures = []

sizes = [expect_elf(p) for p in objects]
print("accepted objects are ELF: " + ", ".join(str(s) for s in sizes))

with tempfile.TemporaryDirectory() as tmp:
    for name, text in REJECTED.items():
        src = Path(tmp) / (name + ".c")
        obj = Path(tmp) / (name + ".o")
        src.write_text(text)
        for opt in ("-O0", "-O2"):
            obj.unlink(missing_ok=True)
            result = subprocess.run(
                cc1 + [
                    "-triple", "mcs251-unknown-none",
                    "-std=gnu11",
                    "-mllvm", "-mcs251-object-format=elf",
                    opt,
                    "-emit-obj",
                    str(src),
                    "-o", str(obj),
                ],
                capture_output=True,
                text=True,
            )
            label = f"{name}{opt}"
            if result.returncode != 1:
                failures.append(
                    f"{label}: expected status 1, got {result.returncode}"
                )
            if obj.exists():
                failures.append(f"{label}: output artifact left behind")
            if result.stderr.strip() == "":
                failures.append(f"{label}: no diagnostic on stderr")
            for needle in CRASH_TEXT:
                if needle in result.stderr:
                    failures.append(f"{label}: crash text {needle!r} present")
            # The rejection must be the unified capability diagnostic, not an
            # incidental error from an unrelated layer.
            if "not supported on MCS251" not in result.stderr:
                failures.append(
                    f"{label}: unexpected diagnostic: {result.stderr[:300]}"
                )

if failures:
    print("FAIL")
    for line in failures:
        print("  " + line)
    sys.exit(1)
print("rejected counterparts: exact status 1, no artifact, clean stderr")
