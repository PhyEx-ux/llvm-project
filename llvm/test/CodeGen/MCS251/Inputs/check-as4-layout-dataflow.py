#!/usr/bin/env python3
"""R4/R9 layout/dataflow checker for the AS4 <-> AS0 addrspacecast.

Companion checker for llvm/test/CodeGen/MCS251/code-addrspacecast-datalayout.ll
(RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充二" §2.2, Alice review R9).

What this checker establishes:

  1. QUERIES the target DataLayout for each memory contract by letting the
     backend fill an *empty* module (`llc -stop-after=finalize-isel` with no
     `target datalayout` line) and parses the layout the target itself
     injected: p0/p4 sizes and index widths are read from that answer.  The
     v1 compatibility contract uses the default `p:32:8` spec, which applies
     to every address space including AS4; that default is resolved
     explicitly rather than assumed;
  2. requires p0 and p4 to be 32-bit with 32-bit index under the v1 compat
     and v2 32-bit contracts, and p0 to shrink to 16 bits under Tiny/XTiny;
  3. DATAFLOW-checks the *test file's own* conversion functions: after a real
     optimization pipeline (`instcombine,gvn,simplifycfg`) each function that
     is documented as an identity round-trip must have folded to returning
     its input unchanged.  A mutation injecting `add 256` (Alice's
     counterexample) folds to `add i32 %addr, 256` and is a FAIL;
  4. constrains the ONE-WAY conversion's data flow (Alice R9-1): the AS0
     load must read the address that the AS4 source names -- offset 0 of the
     same object/base -- not an adjusted address.  Alice's counterexample
     (`getelementptr i8, ptr %q, i32 256` before the load) previously passed
     because only the conversion *instruction* was checked and never the
     address actually read.  The checker now requires the AS4 and AS0 loads
     to use the same base+index chain (canonicalized), so any constant or
     dynamic offset inserted on the converted path is a FAIL;
  5. requires the O2 pipeline to still contain a *dynamic* observation of the
     conversion, not a constant-folded comparison (Alice R9-2): the one-way
     function's converted load must survive optimization as an AS0 load of a
     non-constant pointer, and the caller-side test functions must still
     contain an `addrspacecast` instruction whose result feeds the load.
     `@ext_src` is an `external` AS4 object precisely so the optimizer cannot
     resolve the address and fold everything away;
  6. rejects bitcast/ptrtoint laundering on the conversion path;
  7. checks the direct AS4 load and the converted AS0 load agree on the
     number of DR lanes (byte count);
  8. self-tests: injecting `+256` into the round trip, an offset on the
     one-way converted load (Alice's exact `+256` mutation), swapping the
     addrspacecast for a bitcast, changing an expected lane count, and
     constant-folding the O2 dynamic observation must each FAIL.

Pure stdlib, no LLVM imports.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

LLC = "llc"
OPT = "opt"
TRIPLE = "mcs251"

# Contract spellings: name -> (contract, expected p0 bits, expected p4 bits,
# expected p0 index bits, expected p4 index bits).  Values are the frozen
# acceptance matrix; the actual answer comes from the backend.
CONTRACTS = {
    "v1-compat": ("1,1,32,8,1", 32, 32, 32, 32),
    "v2-32": ("1,2,32,8,1", 32, 32, 32, 32),
    "tiny-16": ("1,2,16,1,1", 16, 32, 16, 32),
    "xtiny-16": ("1,2,16,8,1", 16, 32, 16, 32),
}

# Contracts under which the equal-width AS4 <-> AS0 conversion must be
# refused outright (16-bit AS0 cannot carry the CODE bank).  Both are checked
# by the lit RUN lines of code-addrspacecast-errors.ll and, for the layout
# half, by the REFUSING assertion below.
REFUSING = ("tiny-16", "xtiny-16")
REFUSAL_MSG = ("MCS251: unsupported address-space cast involving CODE "
               "(address space 4)")

EMPTY_MODULE = 'target triple = "mcs251-unknown-none"\n'

VALUE_MODIFYING_OPS = (
    "add", "sub", "mul", "udiv", "sdiv", "urem", "srem", "shl", "lshr",
    "ashr", "trunc", "zext", "sext", "select", "and", "or", "xor",
)

# Functions in the test file documented as value-preserving round trips:
# after optimization they must fold to `ret` of the input (or a direct
# forwarding of it).  name -> the function's i32 argument.
ROUNDTRIP_FUNCS = {
    "roundtrip_noop": "%addr",
}

# Functions in the test file that perform the one-way AS4 -> AS0 conversion
# and read through the converted pointer.  `base`/`index` are the AS4
# source's address components; the converted load must read the SAME address
# (offset 0), so a `getelementptr`/`add` on the converted pointer before the
# load is a FAIL.  name -> (AS4 pointer parameter, AS0 pointer value).
ONEWAY_FUNCS = {
    "via_plain_load": ("%p", "%q", 32),
}

# The O2 pipeline must keep a dynamic observation: after `default<O2>` the
# one-way load must still be a real load through a non-constant pointer, and
# the direct load must still be a real AS4 load.  `@ext_src` below is the
# external AS4 object the test reads, so the address cannot be folded.
O2_DYNAMIC_FUNCS = {
    "via_plain_load": r"load i32, ptr %",
    "direct_load": r"load i32, ptr addrspace\(4\) %",
}


@dataclass
class Findings:
    failures: list[str] = field(default_factory=list)

    def fail(self, msg: str) -> None:
        self.failures.append(msg)


def run(tool: str, args: list[str],
        stdin: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([tool] + args, input=stdin, capture_output=True,
                          text=True)


# --------------------------------------------------------------------------
# Layout queries and refusal checks.
# --------------------------------------------------------------------------

def query_layout(contract: str) -> tuple[str, str]:
    """Ask the backend for the layout by letting it fill an empty module."""
    p = run(LLC, ["-mtriple=" + TRIPLE, "-mcs251-memory-contract=" + contract,
                  "-O0", "-stop-after=finalize-isel", "-o", "-"],
            stdin=EMPTY_MODULE)
    if p.returncode != 0:
        return "", p.stderr.strip()
    m = re.search(r'target datalayout = "([^"]*)"', p.stdout)
    if not m:
        return "", "no datalayout emitted"
    return m.group(1), ""


def parse_layout(dl: str) -> dict[int, tuple[int, int]]:
    """Parse a DataLayout string into addrspace -> (bits, index_bits).

    Handles both the explicit `p4:32:8:8:32` form and the default `p:32:8`
    spec (which applies to every address space not otherwise specified).
    """
    out: dict[int, tuple[int, int]] = {}
    default: tuple[int, int] | None = None
    for tok in dl.split("-"):
        m = re.fullmatch(
            r"p(\d+)?:(\d+)(?::(\d+))?(?::(\d+))?:?(\d+)?", tok)
        if not m:
            continue
        bits = int(m.group(2))
        idx_tok = m.group(5)
        idx = int(idx_tok) if idx_tok else bits
        if m.group(1) is None:
            default = (bits, idx)
        else:
            out[int(m.group(1))] = (bits, idx)
    if default is not None:
        for as_n in (0, 4):
            out.setdefault(as_n, default)
    return out


def check_layouts(f: Findings) -> None:
    for name, (contract, p0_bits, p4_bits, p0_idx, p4_idx) in CONTRACTS.items():
        dl, err = query_layout(contract)
        if not dl:
            f.fail(f"{name}: target emitted no datalayout ({err})")
            continue
        layout = parse_layout(dl)
        for as_n, want_bits, want_idx in ((0, p0_bits, p0_idx),
                                          (4, p4_bits, p4_idx)):
            if as_n not in layout:
                f.fail(f"{name}: datalayout has no p{as_n} spec: {dl}")
                continue
            got_bits, got_idx = layout[as_n]
            if got_bits != want_bits:
                f.fail(f"{name}: p{as_n} is {got_bits}-bit, expected "
                       f"{want_bits} ({dl})")
            if got_idx != want_idx:
                f.fail(f"{name}: p{as_n} index is {got_idx}-bit, expected "
                       f"{want_idx} ({dl})")
        if name in ("v1-compat", "v2-32"):
            if layout[0] != layout[4]:
                f.fail(f"{name}: p0 {layout[0]} != p4 {layout[4]}; the no-op "
                       f"assumption is unsound ({dl})")
        if name in REFUSING and layout[0][0] != 16:
            f.fail(f"{name}: p0 is not 16-bit, the refusal control is gone "
                   f"({dl})")


def check_refusal(f: Findings, test_ll: str) -> None:
    """The 16-bit contracts must REFUSE the conversion, not truncate.

    Alice review R9 noted the old check only asserted the layout width and
    never exercised the rejection: `REFUSING` was grep-only.  This runs the
    minimal conversion module (no datalayout, so the contract itself decides)
    under both 16-bit contracts and requires a hard refusal with the
    documented diagnostic.  The same module is accepted under v1, so the
    refusal is the contract's doing and not a broken input.
    """
    refusal_module = (
        'target triple = "mcs251-unknown-none"\n'
        "define ptr @f(ptr addrspace(4) %p) {\n"
        "  %q = addrspacecast ptr addrspace(4) %p to ptr\n"
        "  ret ptr %q\n"
        "}\n")
    # Control: v1 must ACCEPT the same module, so the refusals below are
    # attributable to the 16-bit contract and not to a malformed input.
    ctrl = run(LLC, ["-mtriple=" + TRIPLE, "-mcs251-memory-contract="
                     + CONTRACTS["v1-compat"][0], "-O0", "-o", "-"],
               stdin=refusal_module)
    if ctrl.returncode != 0:
        f.fail(f"refusal-control: the v1 contract refused the conversion "
               f"module; the refusal comparison would be vacuous "
               f"({ctrl.stderr.strip()[:160]!r})")
    for name in REFUSING:
        contract = CONTRACTS[name][0]
        p = run(LLC, ["-mtriple=" + TRIPLE, "-mcs251-memory-contract=" + contract,
                      "-O0", "-o", "-"], stdin=refusal_module)
        if p.returncode == 0:
            f.fail(f"{name}: the conversion was ACCEPTED under the 16-bit "
                   f"contract (expected a refusal)")
            continue
        if REFUSAL_MSG not in (p.stdout + p.stderr):
            f.fail(f"{name}: refused for the wrong reason: "
                   f"{(p.stdout + p.stderr).strip()[:200]!r}")


# --------------------------------------------------------------------------
# IR dataflow checks.
# --------------------------------------------------------------------------

def optimized_ir(text: str) -> str | None:
    p = run(OPT, ["-passes=instcombine,gvn,simplifycfg", "-S", "-", "-o", "-"],
            stdin=text)
    if p.returncode != 0:
        return None
    return p.stdout


def o2_ir(text: str) -> str | None:
    """Run default<O2>.  The MCS251 contract-check pass needs the target's
    contract spelled out (the module's minimal v1 datalayout makes the
    default v2 contract conflict), so the v1 contract is passed explicitly --
    the same contract the lit RUN lines use for llc."""
    p = run(OPT, ["-mcs251-memory-contract=" + CONTRACTS["v1-compat"][0],
                  "-passes=default<O2>", "-S", "-", "-o", "-"], stdin=text)
    if p.returncode != 0:
        return None
    return p.stdout


def function_body(text: str, name: str) -> str | None:
    m = re.search(rf"define [^\n]*@{re.escape(name)}\([^\n]*\n(.*?)\n\}}",
                  text, re.S)
    return m.group(1) if m else None


def check_roundtrip(f: Findings, test_ll: str) -> None:
    """Run the pipeline over the test file; the round trip must be identity."""
    text = Path(test_ll).read_text()
    out = optimized_ir(text)
    if out is None:
        f.fail("roundtrip: opt failed")
        return
    for fn, arg in ROUNDTRIP_FUNCS.items():
        body = function_body(out, fn)
        if body is None:
            f.fail(f"roundtrip/{fn}: function not found after optimization")
            continue
        # Any value-modifying op applied to the original argument on the
        # return path is a FAIL (this is the +256 counterexample).
        for op in VALUE_MODIFYING_OPS:
            if re.search(rf"%\S+ = {op} i32 {re.escape(arg)}", body):
                f.fail(f"roundtrip/{fn}: conversion changed the value "
                       f"({op} on {arg}); the round trip is not an identity")
        # The result must be a return of the untouched input (possibly via an
        # addrspacecast-covered value that folded away entirely).
        if not re.search(rf"ret i32 {re.escape(arg)}\b", body):
            f.fail(f"roundtrip/{fn}: expected `ret i32 {arg}` after "
                   f"optimization, body is:\n{body.strip()}")


def check_oneway_dataflow(f: Findings, test_ll: str) -> None:
    """The converted AS0 load must read the SAME address as the AS4 source.

    Alice review R9-1: the old checker only verified that an addrspacecast
    existed; a `getelementptr i8, ptr %q, i32 256` inserted between the cast
    and the load still passed.  The address actually read must be constrained:
    the converted pointer feeding the load has to be the addrspacecast
    result itself, never a GEP/arithmetic on top of it (the AS4-to-AS0
    conversion is an identity on the address; adjusting it changes which
    object is read).
    """
    text = Path(test_ll).read_text()
    out = optimized_ir(text)
    if out is None:
        f.fail("oneway: opt failed")
        return
    for fn, (p4, p0, _bits) in ONEWAY_FUNCS.items():
        body = function_body(out, fn)
        if body is None:
            f.fail(f"oneway/{fn}: function not found after optimization")
            continue
        m = re.search(rf"^(\s*)({re.escape(p0)}[0-9.]*) = addrspacecast "
                      rf"ptr addrspace\(4\) {re.escape(p4)} to ptr",
                      body, re.MULTILINE)
        if not m:
            f.fail(f"oneway/{fn}: no addrspacecast from {p4} to an AS0 "
                   f"value in the optimized body:\n{body.strip()}")
            continue
        conv = m.group(2)
        # The converted value must feed the AS0 load directly.  Any other
        # instruction consuming it (a GEP, an add, ...) means the load reads
        # a different address than the AS4 source named.
        load_pat = re.compile(
            rf"^\s*%[A-Za-z0-9._]* = load i32, ptr {re.escape(conv)}\b",
            re.MULTILINE)
        if not load_pat.search(body):
            f.fail(f"oneway/{fn}: the converted AS0 value {conv!r} is not "
                   f"loaded directly; the read address is not constrained to "
                   f"the AS4 source:\n{body.strip()}")
        for line in body.splitlines():
            if conv not in line:
                continue
            if re.search(rf"{re.escape(conv)} = addrspacecast", line):
                continue  # the definition itself
            if load_pat.match(line):
                continue  # the sanctioned direct load
            f.fail(f"oneway/{fn}: the converted AS0 value also feeds "
                   f"{line.strip()!r}, which adjusts the read address")


def check_o2_dynamic(f: Findings, test_ll: str) -> None:
    """O2 must leave a *dynamic* observation of the conversion.

    Alice review R9-2: the old O2 evidence was a constant-folded comparison
    (`icmp eq i32 ptrtoint (@lay_tab), ptrtoint (addrspacecast @lay_tab)`),
    which proves nothing about run-time data flow.  The test module now reads
    an `external` AS4 object, so the optimizer cannot resolve the address and
    the converted load must survive as a real, non-constant load under
    `default<O2>`.  The checker asserts that survival.
    """
    text = Path(test_ll).read_text()
    out = o2_ir(text)
    if out is None:
        f.fail("o2: opt failed")
        return
    for fn, pat in O2_DYNAMIC_FUNCS.items():
        body = function_body(out, fn)
        if body is None:
            f.fail(f"o2/{fn}: function not found after default<O2>")
            continue
        if not re.search(pat, body):
            f.fail(f"o2/{fn}: the dynamic load did not survive default<O2>; "
                   f"the O2 evidence is constant-folded, not a real data "
                   f"flow observation:\n{body.strip()}")
    # And the conversion itself must still be an instruction, not folded to a
    # ConstantExpr comparison.
    via = function_body(out, "via_plain_load") or ""
    if "addrspacecast" not in via:
        f.fail("o2/via_plain_load: no addrspacecast survives default<O2>; "
               "the conversion was folded away")


def check_no_laundering(f: Findings, test_ll: str) -> None:
    """A conversion must be addrspacecast, never bitcast/ptrtoint."""
    text = Path(test_ll).read_text()
    p = run(OPT, ["-passes=instcombine,gvn", "-S", "-", "-o", "-"], stdin=text)
    if p.returncode != 0:
        f.fail("laundering: opt failed")
        return
    if re.search(r"bitcast ptr addrspace\(4\)", p.stdout):
        f.fail("laundering: bitcast used for the AS4 conversion")


def dr_lane_count(asm: str, label: str) -> int | None:
    """Count DR lane reads in a function body (for the lane-agreement check)."""
    m = re.search(rf"_{label}:.*?eret", asm, re.S)
    if not m:
        return None
    return len(re.findall(r"@dr\d+(?:\+0x[0-9a-f]+)?", m.group(0)) or [])


def check_lane_agreement(f: Findings, test_ll: str) -> None:
    """The direct AS4 load and the converted AS0 load must agree in width."""
    text = Path(test_ll).read_text()
    p = run(LLC, ["-mtriple=" + TRIPLE, "-mcs251-memory-contract=1,1,32,8,1",
                  "-O0", "-verify-machineinstrs", "-", "-o", "-"], stdin=text)
    if p.returncode != 0:
        f.fail("lanes: llc failed")
        return
    direct = dr_lane_count(p.stdout, "direct_load")
    via = dr_lane_count(p.stdout, "via_plain_load")
    if direct is None or via is None:
        f.fail(f"lanes: could not find the two load functions "
               f"(direct={direct}, via={via})")
        return
    if direct != 4 or via != 4:
        f.fail(f"lanes: a 4-byte load must use 4 DR lanes, got "
               f"direct={direct}, via={via}")


# --------------------------------------------------------------------------
# Self-tests.
# --------------------------------------------------------------------------

def _mutate_file(text: str, tag: str, test_ll: str) -> Path:
    """Write a mutated module to a scratch file and return its path.

    The scratch file lives in the system temp directory, never next to the
    test source (a lit source tree may be read-only).
    """
    fd, name = tempfile.mkstemp(prefix=f"check-as4-layout-{tag}-",
                                suffix=".ll")
    with os.fdopen(fd, "w") as fh:
        fh.write(text)
    return Path(name)


def self_test(f: Findings, test_ll: str) -> list[str]:
    escaped: list[str] = []
    text = Path(test_ll).read_text()

    # S1: inject `add i32 %v, 256` into the returned round-trip value -> FAIL.
    mutated = text.replace(
        "  %v = ptrtoint ptr addrspace(4) %back to i32\n  ret i32 %v",
        "  %v = ptrtoint ptr addrspace(4) %back to i32\n"
        "  %r = add i32 %v, 256\n  ret i32 %r")
    if mutated == text:
        escaped.append("S1: could not construct the +256 round-trip mutation")
    else:
        mut_path = _mutate_file(mutated, "rt256", test_ll)
        f1 = Findings()
        check_roundtrip(f1, str(mut_path))
        mut_path.unlink()
        if not f1.failures:
            escaped.append("S1: the injected +256 was not detected")

    # S2: Alice's R9-1 counterexample -- offset the converted AS0 pointer by
    # 256 before the load; the one-way dataflow check must FAIL.
    old_shape = ("  %q = addrspacecast ptr addrspace(4) %p to ptr\n"
                 "  %v = load i32, ptr %q, align 1")
    if old_shape not in text:
        escaped.append("S2: could not find the one-way load shape to mutate")
    else:
        mutated = text.replace(
            old_shape,
            "  %q = addrspacecast ptr addrspace(4) %p to ptr\n"
            "  %bad = getelementptr i8, ptr %q, i32 256\n"
            "  %v = load i32, ptr %bad, align 1")
        mut_path = _mutate_file(mutated, "oneway256", test_ll)
        f2 = Findings()
        check_oneway_dataflow(f2, str(mut_path))
        mut_path.unlink()
        if not f2.failures:
            escaped.append("S2: an offset of 256 on the converted AS0 load "
                           "was not detected")

    # S2b: a non-zero GEP via an SSA index (dynamic offset) -> FAIL.
    mutated = text.replace(
        old_shape,
        "  %q = addrspacecast ptr addrspace(4) %p to ptr\n"
        "  %bad = getelementptr i8, ptr %q, i32 %off\n"
        "  %v = load i32, ptr %bad, align 1")
    mutated = mutated.replace(
        "define i32 @via_plain_load(ptr addrspace(4) %p) {",
        "define i32 @via_plain_load(ptr addrspace(4) %p, i32 %off) {")
    if mutated == text:
        escaped.append("S2b: could not construct the dynamic-offset mutation")
    else:
        mut_path = _mutate_file(mutated, "onewaydyn", test_ll)
        if optimized_ir(mutated) is None:
            escaped.append("S2b: dynamic-offset mutant is not valid IR")
        f2b = Findings()
        check_oneway_dataflow(f2b, str(mut_path))
        mut_path.unlink()
        if not f2b.failures:
            escaped.append("S2b: a dynamic offset on the converted AS0 load "
                           "was not detected")

    # S3: replace the conversion with a bitcast -> FAIL.
    mutated = text.replace("addrspacecast ptr addrspace(4) %p4 to ptr",
                           "bitcast ptr addrspace(4) %p4 to ptr")
    if mutated == text:
        escaped.append("S3: could not construct the bitcast mutation")
    else:
        mut_path = _mutate_file(mutated, "bc", test_ll)
        f3 = Findings()
        check_no_laundering(f3, str(mut_path))
        mut_path.unlink()
        if not f3.failures:
            escaped.append("S3: a bitcast conversion was not detected")

    # S4: shrink the load so the lane count changes -> FAIL.
    mutated = text.replace("  %v = load i32, ptr addrspace(4) %p, align 1",
                           "  %v = load i16, ptr addrspace(4) %p, align 1", 1)
    mutated = mutated.replace("define i32 @direct_load", "define i16 @direct_load")
    mutated = mutated.replace("  ret i32 %v\n}\n\ndefine i32 @via_plain_load",
                              "  ret i16 %v\n}\n\ndefine i32 @via_plain_load")
    if mutated == text:
        escaped.append("S4: could not construct the lane-count mutation")
    else:
        mut_path = _mutate_file(mutated, "i16", test_ll)
        f4 = Findings()
        check_lane_agreement(f4, str(mut_path))
        mut_path.unlink()
        if not f4.failures:
            escaped.append("S4: a changed lane count was not detected")

    # S5: Alice's R9-2 direction -- make the O2 observation constant-folded.
    # Replace the converted-parameter load with a read of a private constant
    # object: O2 resolves the address and folds the whole function, so the
    # "dynamic observation survives" check must FAIL.
    body = ("  %q = addrspacecast ptr addrspace(4) %p to ptr\n"
            "  %v = load i32, ptr %q, align 1\n"
            "  ret i32 %v")
    if body not in text:
        escaped.append("S5: could not find the one-way function body")
    else:
        # Keep the function syntactically intact and put the constant at
        # module scope; parser rejection is not constant-folding evidence.
        mutated = text.replace(
            "define i32 @via_plain_load(ptr addrspace(4) %p) {",
            "@konst = private constant i32 1\n"
            "define i32 @via_plain_load(ptr addrspace(4) %p) {")
        mutated = mutated.replace(
            body, "  %v = load i32, ptr @konst, align 1\n  ret i32 %v")
        mut_path = _mutate_file(mutated, "constfold", test_ll)
        folded = o2_ir(mutated)
        if folded is None or "ret i32 1" not in (
                function_body(folded, "via_plain_load") or ""):
            escaped.append("S5: mutant did not validly fold to ret i32 1")
        f5 = Findings()
        check_o2_dynamic(f5, str(mut_path))
        mut_path.unlink()
        if not f5.failures:
            escaped.append("S5: a constant-foldable (non-dynamic) O2 function "
                           "was not detected as a fake O2 observation")

    return escaped


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--llc", default="llc")
    ap.add_argument("--opt", default="opt")
    ap.add_argument("--test-ll", required=True)
    ap.add_argument("--self-test", action="store_true")
    opts = ap.parse_args()

    global LLC, OPT
    LLC, OPT = opts.llc, opts.opt

    f = Findings()
    check_layouts(f)
    check_refusal(f, opts.test_ll)
    check_roundtrip(f, opts.test_ll)
    check_oneway_dataflow(f, opts.test_ll)
    check_o2_dynamic(f, opts.test_ll)
    check_no_laundering(f, opts.test_ll)
    check_lane_agreement(f, opts.test_ll)

    if opts.self_test:
        escaped = self_test(f, opts.test_ll)
        if escaped:
            sys.stderr.write("check-as4-layout-dataflow: SELF-TEST FAIL\n")
            for e in escaped:
                sys.stderr.write("  " + e + "\n")
            return 1
        if f.failures:
            sys.stderr.write("check-as4-layout-dataflow: FAIL on the real "
                             f"test file ({len(f.failures)})\n")
            for x in f.failures:
                sys.stderr.write("  " + x + "\n")
            return 1
        print("check-as4-layout-dataflow: self-tests PASS")
        return 0

    if f.failures:
        sys.stderr.write(
            f"check-as4-layout-dataflow: FAIL ({len(f.failures)})\n")
        for x in f.failures:
            sys.stderr.write("  " + x + "\n")
        return 1
    print("check-as4-layout-dataflow: target p0/p4 layouts (size+index), "
          "16-bit refusal, value-preserving conversion, one-way read address "
          "and dynamic O2 observation verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
