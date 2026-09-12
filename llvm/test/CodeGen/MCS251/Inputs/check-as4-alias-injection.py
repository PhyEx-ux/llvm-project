#!/usr/bin/env python3
"""R8 injection checker for the A3 alias acceptance test.

Companion checker for llvm/test/CodeGen/MCS251/code-addrspacecast-alias.ll
(RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充一", Alice review R8).

The subject under test is the FileCheck oracle in the .ll file itself.  Alice
review R8 showed that the old assertions bound only the final `ret` for some
probes, so injecting a *wrong* optimizer result (`ret i8 0`, a reused old
value, a wrong stored constant) into the unmodified oracle still passed.  This
checker takes the real optimized IR, mutates the *result* the way a defective
optimizer would, and requires the UNMODIFIED .ll oracle to reject each
mutation.  A mutation that still passes means the oracle is not actually
constraining that part of the chain.

Mutations injected (each must FAIL the corresponding check prefix):
  P1/P2/P3  wrong return `ret i8/i16 0` while keeping every store
            (the `new == old -> 0` fold)
  P1/P2/P3  wrong stored constant: the second store writes 42 again
            (a "reuse the old value" defect), return unchanged
  P1/P2/P3  missing second store entirely (the write is dead-code-eliminated)
  P1/P2/P3  wrong store destination @ram, keeping the value and return
  O2 P1-P3  wrong cast source (null), keeping the store's SSA operand
  O2 P1/P2  wrong GEP index (0), keeping the store's SSA operand
  helper    wrong return `ret i8 0` in the helper
  P5        wrong fold: the unrelated base load replaced by the stored value
  C2        the AS4/AS0 read folded to a constant `ret i8 66`
  oracle    wrong constant in the oracle expectations (e.g. store 42 -> 43)
            must itself fail, proving the oracle patterns are load-bearing

The checker is pure stdlib.  It runs `opt` on the real test file and uses the
project's FileCheck, so it validates the shipped oracle rather than a copy.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Pipelines: (name, opt pass pipeline, check prefix used by the .ll file).
PIPELINES = (
    ("gvn", "gvn", "GVN"),
    ("pipe", "instcombine,early-cse,gvn,simplifycfg", "PIPE"),
    ("o2", "default<O2>", "O2"),
)

# Probes whose full chain (old value, stored value, new value, return) is
# asserted.  The `i8`/`i16` spellings select the return-type mutation.
FULL_CHAIN_FUNCS = (
    ("probe_lsl_same", "i8"),
    ("probe_lsl_gep", "i8"),
    ("probe_lsl_wide", "i16"),
)


def run(tool: str, args: list[str], stdin: str | None = None):
    return subprocess.run([tool] + args, input=stdin, capture_output=True,
                          text=True)


def opt_pipeline(opt: str, passes: str, text: str) -> str | None:
    p = run(opt, ["-passes=" + passes, "-S", "-", "-o", "-"], stdin=text)
    if p.returncode != 0:
        return None
    return p.stdout


def run_filecheck(fc_tool: str, check_file: Path, prefix: str,
                  text: str) -> bool:
    with tempfile.NamedTemporaryFile("w", suffix=".ll", delete=False) as fh:
        fh.write(text)
        inp = fh.name
    try:
        p = run(fc_tool, [str(check_file), "--check-prefix=" + prefix,
                          "--input-file=" + inp])
        return p.returncode == 0
    finally:
        Path(inp).unlink(missing_ok=True)


def func_body(text: str, name: str) -> str | None:
    m = re.search(rf"define [^\n]*@{re.escape(name)}\([^\n]*\n(.*?)\n\}}", text,
                  re.S)
    return m.group(0) if m else None


def replace_body(text: str, name: str, new_body: str) -> str | None:
    m = re.search(rf"define [^\n]*@{re.escape(name)}\([^\n]*\n.*?\n\}}", text,
                  re.S)
    if not m:
        return None
    return text[:m.start()] + new_body + text[m.end():]


def require_fail(escaped: list[str], fc_tool: str, check_file: Path,
                 prefix: str, mutated: str, what: str) -> None:
    if run_filecheck(fc_tool, check_file, prefix, mutated):
        escaped.append(f"{what}: the oracle accepted the wrong result")


def require_pass(escaped: list[str], fc_tool: str, check_file: Path,
                 prefix: str, text: str, what: str) -> None:
    if not run_filecheck(fc_tool, check_file, prefix, text):
        escaped.append(f"{what}: the oracle rejected the correct result "
                       f"(baseline broken)")


def check_injections(opt: str, fc_tool: str, test_ll: Path) -> list[str]:
    escaped: list[str] = []
    text = test_ll.read_text()

    for pname, passes, prefix in PIPELINES:
        out = opt_pipeline(opt, passes, text)
        if out is None:
            escaped.append(f"{pname}: opt failed")
            continue
        # The unmodified oracle must accept the real output.
        require_pass(escaped, fc_tool, test_ll, prefix, out,
                     f"{pname}/baseline")

        for fn, ret_ty in FULL_CHAIN_FUNCS:
            body = func_body(out, fn)
            if body is None:
                escaped.append(f"{pname}/{fn}: function not found")
                continue

            # Injection 1: wrong return 0 with every store kept.
            if not re.search(rf"ret {ret_ty} -35\b", body):
                escaped.append(
                    f"{pname}/{fn}: the optimized body does not return the "
                    f"documented -35; cannot inject `ret 0` "
                    f"(body={body.strip()[:120]!r})")
                continue
            wrong_ret = re.sub(rf"ret {ret_ty} -35\b", f"ret {ret_ty} 0", body)
            mutated = replace_body(out, fn, wrong_ret)
            require_fail(escaped, fc_tool, test_ll, prefix, mutated,
                         f"{pname}/{fn}/wrong-return-0")

            # Injection 2: wrong stored constant -- the write that should be 7
            # is folded back to the *old* value's constant (a stale-value
            # defect).  The stores are expected as `store i8 7, ptr`; rewrite
            # the last one to the old constant.
            if f"store {ret_ty} 7, ptr" in body:
                stale = re.sub(rf"store {ret_ty} 7, ptr",
                               f"store {ret_ty} 42, ptr", body)
                mutated = replace_body(out, fn, stale)
                require_fail(escaped, fc_tool, test_ll, prefix, mutated,
                             f"{pname}/{fn}/stale-store")
            else:
                escaped.append(
                    f"{pname}/{fn}: no `store {ret_ty} 7` to mutate; the "
                    f"stored-value half of the chain is not observable")

            # Injection 3: drop the write entirely (dead-store defect).
            lines = [l for l in body.splitlines()
                     if not re.match(rf"\s*store {ret_ty} 7, ptr", l)]
            mutated = replace_body(out, fn, "\n".join(lines))
            require_fail(escaped, fc_tool, test_ll, prefix, mutated,
                         f"{pname}/{fn}/missing-store")

            # Injection 3b: preserve the correct folded return but redirect
            # the final write to an unrelated object.  A constant-only store
            # pattern accepts this genuine wrong-address miscompile; bind the
            # destination to the converted parameter in every pipeline.
            wrong_dest, n = re.subn(
                rf"(store {ret_ty} 7, ptr )%[A-Za-z0-9._]+(?=,)",
                r"\g<1>@ram", body)
            if n != 1:
                escaped.append(f"{pname}/{fn}: cannot construct exactly one "
                               "wrong-store-destination mutation")
            else:
                require_fail(escaped, fc_tool, test_ll, prefix,
                             replace_body(out, fn, wrong_dest),
                             f"{pname}/{fn}/wrong-store-destination")

            # Injection 3c (O2): a store-operand check alone is insufficient
            # if the SSA producer silently changes.  Exercise both links of
            # the surviving %base -> cast -> GEP(%i) -> store chain.  P3 has
            # no GEP.  Mutate each link independently in the real output.
            if prefix == "O2":
                address_mutations = [
                    (r"(addrspacecast ptr addrspace\(4\) )%base(?= to ptr)",
                     r"\g<1>null", "wrong-cast-source"),
                ]
                if fn != "probe_lsl_wide":
                    address_mutations.append(
                        (r"(getelementptr i8, ptr %[A-Za-z0-9._]+, i32 )%i\b",
                         r"\g<1>0", "wrong-gep-index"))
                for pattern, replacement, kind in address_mutations:
                    bad_address, n = re.subn(pattern, replacement, body)
                    if n != 1:
                        escaped.append(f"{pname}/{fn}: cannot construct "
                                       f"exactly one {kind} mutation")
                    else:
                        require_fail(escaped, fc_tool, test_ll, prefix,
                                     replace_body(out, fn, bad_address),
                                     f"{pname}/{fn}/{kind}")

        # Bind the documented noinline property to the emitted helper, not
        # merely to the source spelling.  The surviving call/body assertions
        # above already guard observation; this verifies the stated mechanism.
        helper = func_body(out, "lsl_helper") or ""
        header = helper.splitlines()[0] if helper else ""
        attr = re.search(r"#(\d+)", header)
        attr_line = re.search(
            rf"^attributes #{attr.group(1)} = (.+)$", out, re.MULTILINE
        ) if attr else None
        if not re.search(r"\bnoinline\b", header + " " + (
                attr_line.group(1) if attr_line else "")):
            escaped.append(f"{pname}/lsl_helper: noinline attribute missing")

        # Injection 4: the noinline helper's chain.  A wrong `ret 0` there is
        # the "helper observes nothing" defect.
        body = func_body(out, "lsl_helper")
        if body and "ret i8" in body:
            mutated = replace_body(out, "lsl_helper",
                                   re.sub(r"ret i8 [^\n]+", "ret i8 0", body))
            require_fail(escaped, fc_tool, test_ll, prefix, mutated,
                         f"{pname}/lsl_helper/wrong-return-0")
        else:
            escaped.append(f"{pname}/lsl_helper: helper body not found")

        # Injection 5: P5 -- forward the unrelated AS0 store into the AS4 read
        # (the wrong-cross-space-must-alias defect).
        body = func_body(out, "probe_other_object")
        if body:
            bad = body.replace("load i8, ptr addrspace(4) %base",
                               "load i8, ptr @ram")
            if bad == body:
                escaped.append(
                    f"{pname}/probe_other_object: could not inject the "
                    f"wrong forward (load form changed)")
            else:
                require_fail(escaped, fc_tool, test_ll, prefix,
                             replace_body(out, "probe_other_object", bad),
                             f"{pname}/probe_other_object/wrong-forward")

        # Injection 6: C2 -- fold the CODE read to a constant.
        for fn in ("legal_code_read_as4", "legal_code_read_as0"):
            body = func_body(out, fn)
            if body is None:
                escaped.append(f"{pname}/{fn}: function not found")
                continue
            if re.search(r"load i8, ptr", body):
                folded = re.sub(r"\s*%v = load i8, ptr[^\n]*\n", "\n", body)
                folded = re.sub(r"ret i8 %v", "ret i8 66", folded)
                require_fail(escaped, fc_tool, test_ll, prefix,
                             replace_body(out, fn, folded),
                             f"{pname}/{fn}/folded-constant")

    # Oracle mutation: change an expected constant in the check lines and
    # require the real (correct) optimized output to fail the mutated oracle.
    # This proves the constants in the oracle are actually load-bearing.
    mutated_oracle = "\n".join(
        l.replace("store i8 7, ptr", "store i8 9, ptr")
        if l.lstrip().startswith(("; GVN:", "; PIPE:", "; O2:")) else l
        for l in text.splitlines())
    if mutated_oracle == text:
        escaped.append("oracle-mutation: no expectation matched the rewrite "
                       "pattern; the store constant is not asserted")
    else:
        fd, name = tempfile.mkstemp(prefix="check-as4-alias-oracle-",
                                    suffix=".ll")
        with os.fdopen(fd, "w") as fh:
            fh.write(mutated_oracle)
        mut_file = Path(name)
        try:
            for pname, passes, prefix in PIPELINES:
                out = opt_pipeline(opt, passes, text)
                if out is None:
                    continue
                if run_filecheck(fc_tool, mut_file, prefix, out):
                    escaped.append(
                        f"oracle-mutation/{pname}: an oracle with the stored "
                        f"constant changed to 9 still accepted the real "
                        f"output; the constant is not asserted")
            # A negative control on the mutation itself: with the *correct*
            # oracle the same output passes.
            for pname, passes, prefix in PIPELINES:
                out = opt_pipeline(opt, passes, text)
                if out is not None:
                    require_pass(escaped, fc_tool, test_ll, prefix, out,
                                 f"oracle-mutation-control/{pname}")
        finally:
            mut_file.unlink(missing_ok=True)

    return escaped


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--opt", default="opt")
    ap.add_argument("--filecheck", default="FileCheck")
    ap.add_argument("--test-ll", required=True)
    ap.add_argument("--self-test", action="store_true",
                    help="accepted for symmetry; the injection battery is "
                         "the default mode of this checker")
    opts = ap.parse_args()

    test_ll = Path(opts.test_ll)
    escaped = check_injections(opts.opt, opts.filecheck, test_ll)
    if escaped:
        sys.stderr.write(f"check-as4-alias-injection: FAIL "
                         f"({len(escaped)} escapes)\n")
        for e in escaped:
            sys.stderr.write("  " + e + "\n")
        return 1
    print("check-as4-alias-injection: every injected wrong result (ret 0, "
          "stale store, missing store, wrong-store-destination, "
          "wrong-cast-source, wrong-gep-index, wrong forward, folded constant) "
          "is rejected by the unmodified FileCheck oracle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
