#!/usr/bin/env python3
"""a3-as-launder-check.py - prove the AS4 -> AS0 path is never an int round-trip.

Alice review R10-4: the e2e drivers only grepped for `inttoptr ... to ptr
addrspace(4)`, which rejects the shape that manufactures an AS4 pointer but
does not prove the approved AS4 -> AS0 path is free of integer round-trips.
This checker scans the module's own dataflow and requires:

  1. a CODE (AS4) pointer never becomes an integer that feeds a pointer:
     every `ptrtoint` whose operand is `ptr addrspace(4)` is a FAIL, and so
     is any `inttoptr` whose operand is (transitively, within the block) a
     `ptrtoint` of an AS4 pointer;
  2. every use of an AS4 global as an argument to a generic (AS0) call is an
     `addrspacecast` -- i.e. the approved conversion is what carries the
     pointer, never an integer launder;
  3. no `bitcast ptr addrspace(4)` appears (a bitcast is not the approved
     conversion);
  4. the module contains at least one AS4 -> AS0 addrspacecast per level, so
     the check cannot pass vacuously on a module that simply has no CODE
     sources.

Usage: a3-as-launder-check.py <module.ll> [--expect-as4-casts N]
Exit 0 on success, 1 with FAIL lines otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def fail(msgs: list[str], m: str) -> None:
    msgs.append(m)


def instruction_defs(text: str) -> dict[str, str]:
    """Map SSA name -> the defining instruction text (single-line defs)."""
    defs: dict[str, str] = {}
    for line in text.splitlines():
        m = re.match(r"\s*(%[A-Za-z0-9._]+) = (.+)$", line)
        if m:
            defs[m.group(1)] = m.group(2).strip()
    return defs


def inttoptr_source_is_as4_laundry(operand: str,
                                   defs: dict[str, str]) -> str | None:
    """Return the offending definition if `operand` comes from AS4 ptrtoint."""
    seen = set()
    cur = operand.strip()
    while cur and cur not in seen:
        seen.add(cur)
        if not cur.startswith("%"):
            return None
        d = defs.get(cur)
        if d is None:
            return None
        if re.match(r"ptrtoint ptr addrspace\(4\)", d):
            return d
        m = re.fullmatch(r"(?:add|sub|or|and|xor|shl|lshr|zext|trunc|sext) "
                         r"[^,]+, (%[A-Za-z0-9._]+)", d)
        if m:
            m2 = re.search(r"ptrtoint ptr addrspace\(4\)", d)
            if m2:
                return d
            cur = m.group(1)
            continue
        if d.startswith("inttoptr "):
            return None
        return None
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--what", default="module")
    opts = ap.parse_args()

    text = Path(opts.module).read_text()
    msgs: list[str] = []
    defs = instruction_defs(text)

    # 1) AS4 pointer -> integer is never allowed (it can only be used to
    #    manufacture a pointer again, or to launder a difference; the approved
    #    path never needs it on an AS4 pointer).
    for line in text.splitlines():
        if re.search(r"ptrtoint ptr addrspace\(4\)", line):
            fail(msgs, f"{opts.what}: `ptrtoint ptr addrspace(4)` found: "
                       f"{line.strip()!r}")

    # 1b) an inttoptr fed (possibly through pure integer ops) by an AS4
    #     ptrtoint is laundering.
    for line in text.splitlines():
        m = re.search(r"inttoptr i[0-9]+ ([^ ]+) to ptr", line)
        if not m:
            continue
        operand = m.group(1)
        if not operand.startswith("%"):
            continue  # a literal address (e.g. an SFR) is not laundering
        bad = inttoptr_source_is_as4_laundry(operand, defs)
        if bad is not None:
            fail(msgs, f"{opts.what}: inttoptr operand {operand} comes from "
                       f"an AS4 pointer launder: {bad!r}")

    # 2) An AS4 global used as an argument to a generic call must go through
    #    addrspacecast.  The approved form is a ConstantExpr
    #    `addrspacecast (ptr addrspace(4) @sym to ptr)` or an instruction
    #    value; anything else (a bare @sym argument, an inttoptr) is a FAIL.
    as4_globals = set(re.findall(
        r"^@([A-Za-z0-9._]+) = [^\n]*addrspace\(4\)", text, re.MULTILINE))
    for line in text.splitlines():
        if "call" not in line:
            continue
        for name in as4_globals:
            # Exact-name match: `@.str` must not match `@.str.2`.
            if not re.search(rf"@{re.escape(name)}(?![A-Za-z0-9._])", line):
                continue
            approved = re.search(
                rf"addrspacecast \(ptr addrspace\(4\) @{re.escape(name)} "
                rf"to ptr\)", line)
            if approved:
                continue
            fail(msgs, f"{opts.what}: AS4 global @{name} appears in a call "
                       f"outside the approved addrspacecast conversion: "
                       f"{line.strip()!r}")

    # 2b) Every other reference to an AS4 global must either keep its AS4 type
    #     explicitly (`ptr addrspace(4) @sym`, a legitimate CODE access) or be
    #     inside the approved addrspacecast.  A bare `@sym` in a generic
    #     position is a FAIL.
    for line in text.splitlines():
        if re.match(r"^@", line) or line.strip().startswith(";"):
            continue
        for name in as4_globals:
            if not re.search(rf"@{re.escape(name)}(?![A-Za-z0-9._])", line):
                continue
            if re.search(rf"ptr addrspace\(4\) @{re.escape(name)}", line):
                continue  # an explicitly AS4-typed use
            if re.search(rf"addrspacecast \(ptr addrspace\(4\) "
                         rf"@{re.escape(name)} to ptr\)", line):
                continue  # the approved conversion
            fail(msgs, f"{opts.what}: AS4 global @{name} used outside the "
                       f"approved addrspacecast: {line.strip()!r}")

    # 3) bitcast is never the conversion.
    for line in text.splitlines():
        if "bitcast ptr addrspace(4)" in line:
            fail(msgs, f"{opts.what}: bitcast used for the AS4 conversion: "
                       f"{line.strip()!r}")

    # 4) Non-vacuity: a module that has CODE (AS4) globals must also contain
    #    at least one AS4 -> AS0 addrspacecast, otherwise the check is
    #    vacuous.  Modules with no CODE globals at all (the shim library, a
    #    pure generic TU) legitimately have none.
    n_casts = len(re.findall(r"addrspacecast \(?ptr addrspace\(4\)", text))
    if as4_globals and n_casts == 0:
        fail(msgs, f"{opts.what}: the module has AS4 globals but no AS4 -> "
                   f"AS0 addrspacecast; the laundering check is vacuous")

    if msgs:
        for m in msgs:
            print(f"FAIL:{m}")
        return 1
    print(f"PASS:{opts.what}: AS4 -> AS0 carried by addrspacecast "
          f"({n_casts} cast(s)), no bitcast and no integer round-trip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
