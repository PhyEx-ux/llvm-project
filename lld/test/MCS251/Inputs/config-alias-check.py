#!/usr/bin/env python3
"""Persistent regression: repeated `--isr-preemption-config` across path ALIASES.

Round-7 review item (suggestion 1).  The (z10) group already pins the reject
for a repeated option with the same path, a HARD link and a SYMBOLIC link.
The review used three further spellings that a path-comparison shortcut would
have to handle, and none of them was in the persistent suite:

  1. an ABSOLUTE first occurrence against a LEXICALLY DIFFERENT relative
     spelling of the same file (`real/../real/A.txt`);
  2. a spelling reached through a SYMLINKED DIRECTORY (`via/A.txt`, where
     `via` is a symlink to `real`);
  3. a spelling that is an INHERITED FILE DESCRIPTOR
     (`/proc/self/fd/<n>`), which names the same file without any path
     component to compare at all.  This spelling is Linux-specific and is
     SKIPPED when `/proc/self/fd` is unavailable, so the check can report
     "not covered here" instead of failing on a platform that lacks it.

The reject is on the OPTION, so it must fire BEFORE any path identity is
consulted -- which is exactly what makes these spellings a meaningful
regression: it must not matter whether the two occurrences are recognisable
as one file.

For every combination of spelling and output mode the helper asserts:
  * the exit status is EXACTLY 1;
  * stderr carries the duplicate-option diagnostic;
  * the contract file's bytes are UNCHANGED (byte identity, not size);
  * no ELF and no placement report was published.

It prints one machine-readable line per case plus a total, exits 0 only when
every case passed, and exits 3 when the inherited-descriptor spelling had to
be skipped (so the caller can see the gap rather than read a silent pass).

Usage: config-alias-check.py <mcs251-lld> <crt.o> <n2.o> <workdir>
"""
import os
import shutil
import subprocess
import sys

AREAS = ["--area-start=HOME=0xff0000", "--area-start=BOOT=0xff0500",
         "--area-start=CSEG=0xff0700", "--area-start=XINIT=0xff8000"]
MODES = [
    ("plain", []),
    ("audit", ["--placement-report=r.txt"]),
    ("verify", ["--verify-placement"]),
    ("off", ["--isr-reentrancy=off"]),
]
DUP_DIAG = ("duplicate --isr-preemption-config: the preemption contract is a "
            "single input file")


def main():
    if len(sys.argv) != 5:
        sys.stderr.write(__doc__.splitlines()[-1].strip() + "\n")
        return 2
    lld, crt, n2, work = sys.argv[1:5]
    exe = shutil.which(lld) or lld
    if not os.path.exists(crt) or not os.path.exists(n2):
        sys.stderr.write("config-alias-check: missing object inputs\n")
        return 2

    os.makedirs(work, exist_ok=True)
    real = os.path.join(work, "real")
    os.makedirs(real, exist_ok=True)
    contract = os.path.join(real, "A.txt")
    with open(contract, "w") as f:
        f.write("preempt _irq1 _irq2\n")
    orig = open(contract, "rb").read()
    # `B.txt` is the second, comment-only contract shape from the finding; it
    # is not needed for the alias spellings but keeps the directory shape the
    # same as the (z10) group.
    with open(os.path.join(work, "B.txt"), "w") as f:
        f.write("# B: comments only\n")
    via = os.path.join(work, "via")
    if not os.path.islink(via) and not os.path.exists(via):
        os.symlink(real, via)

    results = []
    skipped = []

    def one(tag, second_spelling, pass_fds=(), cwd=None):
        for mode, mode_opts in MODES:
            elf = os.path.join(work, "out.elf")
            report = os.path.join(work, "r.txt")
            for stale in (elf, report):
                if os.path.exists(stale):
                    os.unlink(stale)
            argv = [exe, crt, n2] + AREAS + [
                "--isr-preemption-config=" + contract,
                "--isr-preemption-config=" + second_spelling,
                "--link-facts=" + contract,
            ] + mode_opts + ["-o", elf]
            p = subprocess.run(argv, cwd=cwd or work, capture_output=True,
                               pass_fds=tuple(pass_fds))
            err = p.stderr.decode(errors="replace")
            intact = open(contract, "rb").read() == orig
            case = {
                "spelling": tag, "mode": mode, "rc": p.returncode,
                "contract_intact": intact,
                "no_elf": not os.path.exists(elf),
                "no_report": not os.path.exists(report),
                "diag": DUP_DIAG in err,
            }
            case["ok"] = (case["rc"] == 1 and intact and case["no_elf"]
                          and case["no_report"] and case["diag"])
            results.append(case)

    # 1. absolute first occurrence against a lexically different RELATIVE one
    one("relative-lexical", "real/../real/A.txt")
    # 2. reached through a SYMLINKED DIRECTORY
    one("symlink-dir", via + "/A.txt")
    # 3. an INHERITED FILE DESCRIPTOR (Linux only)
    if os.path.isdir("/proc/self/fd"):
        fd = os.open(contract, os.O_RDONLY)
        try:
            one("inherited-fd", "/proc/self/fd/%d" % fd, pass_fds=(fd,))
        finally:
            os.close(fd)
    else:
        skipped.append("inherited-fd (no /proc/self/fd on this platform)")

    for c in results:
        sys.stdout.write(
            "ALIAS spelling=%s mode=%s rc=%d contract_intact=%d no_elf=%d "
            "no_report=%d diag=%d ok=%d\n"
            % (c["spelling"], c["mode"], c["rc"], c["contract_intact"],
               c["no_elf"], c["no_report"], c["diag"], c["ok"]))
    bad = [c for c in results if not c["ok"]]
    sys.stdout.write("ALIAS cases=%d failed=%d skipped=%d\n"
                     % (len(results), len(bad), len(skipped)))
    for s in skipped:
        sys.stdout.write("ALIAS SKIPPED %s\n" % s)
    if bad:
        for c in bad:
            sys.stderr.write("config-alias-check: FAILED %s %s\n"
                             % (c["spelling"], c["mode"]))
        return 1
    return 3 if skipped else 0


if __name__ == "__main__":
    sys.exit(main())
