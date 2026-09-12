#!/usr/bin/env python3
"""A2c cross-TU checker: signature/address-space/const agreement.

Companion checker for clang/test/CodeGen/mcs251-as4-cross-tu.c
(RUNTIME-AS-PTR-DESIGN-A.md §2.7, §3-A2c).  The header, the caller TU and the
callee/definition TU are compiled SEPARATELY; this checker then proves, from
the two modules' own output, that:

  1. both modules match a COMPLETE, independently frozen signature oracle
     (parameter list *and* return type, per function), not merely each other.
     The old caller-vs-callee equality was satisfiable by mutating both sides
     together (Alice review R7); the oracle below is a literal IR text table
     that neither module can influence.  In particular `generic_read` is
     pinned to the generic AS0 pointer type `ptr`: a `ptr addrspace(3)` pair
     on both sides is a FAIL, not "agreement";
  2. the CODE entry points carry AS4 on the pointee/return on both sides and
     the generic entry point stays AS0 on both sides (address space is part
     of the oracle, so it cannot drift);
  3. the caller's AS4 -> AS0 use sites materialise as addrspacecast (never a
     bitcast or an integer round-trip) and the AS4 sources stay AS4;
  4. the callee never stores into its CODE objects (no AS4 store);
  5. pointee const is proven from the SOURCE (AST), not from IR: opaque
     pointer IR does not carry C pointee const, so an AST probe with the
     header's own declarations must reject writes through the implicit-const
     CODE pointer and accept writes through the generic AS0 pointer;
  6. the implicit-const declaration TU and explicit-const definition TU
     produce identical ABI signatures (`uint8 __code *` vs
     `const uint8 __code *`).

Fail-closed self-tests (`--self-test`) mutate the expected tables and the
inputs and require every mutation to be caught:
  M1 drop const from a callee expectation; M2 change an address space;
  M3 drop an addrspacecast from the caller expectations; M4 accept a bitcast
  as a conversion; M5 forget a required function; M6 accept an AS4 store;
  M7 mutate BOTH sides to AS3 generic (Alice's R7 counterexample) -> the
  oracle must still fail; M8 drop the return type from the oracle -> the
  oracle must fail structurally; M9 weaken AS4 expectation to accept a
  bitcast -> must fail.

The checker is pure stdlib and does not import LLVM product code.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------
# Frozen expectations (A2a/A2c types).  Each entry is the *printed LLVM IR*
# of the declared function, i.e. the ABI-visible signature.
# --------------------------------------------------------------------------

CALLEE_DEFS = {
    "code_lookup": r"define [^\n]*ptr addrspace\(4\)[^\n]*@code_lookup\(i32",
    "code_read": r"define [^\n]*zeroext i8 @code_read\(ptr addrspace\(4\)",
    "code_base": r"define [^\n]*ptr addrspace\(4\)[^\n]*@code_base\(\)",
    "generic_read": r"define [^\n]*zeroext i8 @generic_read\(ptr",
    # The table itself: AS4, constant, 8 bytes.
    "shared_rom": r"@shared_rom = (?:dso_local )?addrspace\(4\) constant \[8 x i8\]",
}

# The callee must define exactly this set of entry points: an extra or
# missing definition is a FAIL (M5 in the self-tests).
CALLEE_ENTRIES = ("code_lookup", "code_read", "code_base", "generic_read")

CALLEE_FORBIDDEN = {
    # A CODE object must never be stored into (an AS4 *pointer operand*).
    "as4-store": r"store [^,\n]*, ptr addrspace\(4\)",
    # Nor may the table lose AS4/const.
    "plain-table": r"@shared_rom = (?:dso_local )?global \[8 x i8\]",
    "mutable-table": r"@shared_rom = (?:dso_local )?addrspace\(4\) global",
}

CALLER_DECLS = {
    "code_lookup": r"declare (?:dso_local )?ptr addrspace\(4\) @code_lookup\(i32",
    "code_read": r"declare (?:dso_local )?zeroext i8 @code_read\(ptr addrspace\(4\)",
    "code_base": r"declare (?:dso_local )?ptr addrspace\(4\) @code_base\(\)",
    "generic_read": r"declare (?:dso_local )?zeroext i8 @generic_read\(ptr",
}

# The caller must convert, not reinterpret: at least one addrspacecast from
# AS4 to AS0, and no bitcast/ptrtoint between the two spaces.
CALLER_REQUIRED = {
    "as4-to-as0-cast": r"addrspacecast \(?ptr addrspace\(4\)",
    "generic-call": r"call (?:dso_local )?zeroext "
                    r"(?:addrspace\(\d+\) )?i8 @generic_read\(ptr",
}
CALLER_FORBIDDEN = {
    "bitcast-conversion": r"bitcast ptr addrspace\(4\)",
    "ptrtoint-launder": r"ptrtoint ptr addrspace\(4\)",
    "as4-store": r"store [^,\n]*, ptr addrspace\(4\)",
    "as0-to-as4-implicit": r"addrspacecast ptr %\S+ to ptr addrspace\(4\)",
}

# --------------------------------------------------------------------------
# The INDEPENDENT signature oracle (Alice review R7).
#
# `SIG_ORACLE[name]` is the complete ABI-visible signature: return type,
# every parameter type in order, and the address space of each.  It is
# literal text, typed out from the frozen A2a types -- NOT derived from
# either module.  The comparison is on a normalized token stream so that
# optimizer-added attributes (`noundef`, `captures(none)`, `dso_local`,
# `local_unnamed_addr`, `#0`, ...) do not matter, while the *types* must
# match character for character.
#
# `generic_read` is pinned to `ptr` -- i.e. AS0.  `ptr addrspace(3)` (or any
# other space) on BOTH sides fails the oracle, which is exactly the escape
# the caller-vs-callee equality could not see.
#
# Each entry is (return tokens, parameter-type list): BOTH halves are frozen
# and compared, so dropping the return type is not an escape either.
# --------------------------------------------------------------------------

SIG_ORACLE: dict[str, tuple[str, tuple[str, ...]]] = {
    "code_lookup": ("ptr addrspace(4)", ("i32",)),
    "code_read": ("i8", ("ptr addrspace(4)", "i32")),
    "code_base": ("ptr addrspace(4)", ()),
    "generic_read": ("i8", ("ptr", "i32")),
}

# Params/returns of the CODE entry points must carry AS4; the generic entry
# point must NOT carry any address space on its pointee (AS0 is the default
# generic space spelled without a qualifier).
AS4_ENTRIES = ("code_lookup", "code_read", "code_base")
GENERIC_ENTRIES = ("generic_read",)

# Source-level AST oracle for pointee const: opaque pointer IR erases C
# pointee const, so this half is observed on the AST of the shared header
# compiled as its own TU.  `code_read` is declared with the implicit-const
# CODE pointee; `generic_read` with the plain AS0 pointer.  The probe bodies
# must be rejected/accepted respectively (the write-through-const rule).
AST_CALLEE_TYPE = "const __attribute__((address_space(4))) uint8"
AST_CALLEE_READ_DECL = (
    "uint8 (const __attribute__((address_space(4))) uint8 *, unsigned int)")
AST_GENERIC_DECL = "uint8 (const uint8 *, unsigned int)"

# `uint8 __code *` (implicit const) must produce the SAME canonical type as
# `const uint8 __code *` (explicit const): the implicit const is part of the
# CODE pointee, so both spellings are one type.
AST_IMPLICIT_EQ_EXPLICIT = (
    ("uint8 __code *p", "const __attribute__((address_space(4))) uint8 *"),
    ("const uint8 __code *p", "const __attribute__((address_space(4))) uint8 *"),
)

CONST_WRITE_PROBES = {
    # (name, source, expected_error) -- must be REJECTED.
    "header-implicit-code-write":
        ("#include \"mcs251-as4-cross-tu.h\"\n"
         "void probe(uint8 __code *p) { *p = 1; }\n",
         "read-only variable is not assignable"),
    "explicit-const-code-write":
        ("#include \"mcs251-as4-cross-tu.h\"\n"
         "void probe(const uint8 __code *p) { *p = 1; }\n",
         "read-only variable is not assignable"),
}
CONST_CLEAN_PROBES = {
    # Must be ACCEPTED: a generic AS0 pointer is writable, so the rejection
    # above is the CODE pointee's const, not a blanket write ban.
    "generic-as0-write":
        ("#include \"mcs251-as4-cross-tu.h\"\n"
         "void probe(uint8 *p) { *p = 1; }\n"),
}


@dataclass
class Findings:
    failures: list[str] = field(default_factory=list)

    def require(self, text: str, what: str, pattern: str) -> None:
        if not re.search(pattern, text):
            self.failures.append(f"{what}: missing {pattern!r}")

    def forbid(self, text: str, what: str, pattern: str) -> None:
        if re.search(pattern, text):
            self.failures.append(f"{what}: forbidden {pattern!r} present")


def check_impl_module(text: str, f: Findings) -> None:
    for what, pat in CALLEE_DEFS.items():
        f.require(text, f"callee/{what}", pat)
    for what, pat in CALLEE_FORBIDDEN.items():
        f.forbid(text, f"callee/{what}", pat)
    # Exactly the frozen entry set, no more and no fewer definitions.
    defined = set(re.findall(r"^define [^\n]*@([A-Za-z0-9_]+)\(", text,
                             re.MULTILINE))
    missing = set(CALLEE_ENTRIES) - defined
    extra = defined - set(CALLEE_ENTRIES)
    if missing:
        f.failures.append(f"callee/entries: missing definitions {sorted(missing)}")
    if extra:
        f.failures.append(f"callee/entries: unexpected definitions {sorted(extra)}")


def check_caller_module(text: str, f: Findings) -> None:
    for what, pat in CALLER_DECLS.items():
        f.require(text, f"caller/{what}", pat)
    for what, pat in CALLER_REQUIRED.items():
        f.require(text, f"caller/{what}", pat)
    for what, pat in CALLER_FORBIDDEN.items():
        f.forbid(text, f"caller/{what}", pat)


# --------------------------------------------------------------------------
# Signature extraction.
# --------------------------------------------------------------------------

# Attributes the optimizer adds at -O2 or that are irrelevant to the ABI
# *type* comparison.  Stripped as whole tokens (never as substrings of a type
# name), so a dropped `addrspace(4)` is still visible.  `zeroext`/`signext`
# are ABI attributes asserted separately by the raw CALLEE_DEFS/CALLER_DECLS
# patterns, so they do not participate in the type oracle.
ATTRS = {
    "noundef", "nofree", "nosync", "nounwind", "readonly", "writeonly",
    "readnone", "willreturn", "nocallback", "noalias", "nonnull",
    "signext", "zeroext", "inreg", "immarg", "noreturn", "cold", "hot",
    "dso_local", "local_unnamed_addr", "norecurse",
    "mustprogress", "memory",
}


def _strip_attrs(sig: str) -> str:
    """Remove ABI-irrelevant attributes from a parameter/return token list."""
    sig = re.sub(r"captures\([^)]*\)", " ", sig)
    sig = re.sub(r"dereferenceable\(\d+\)", " ", sig)
    sig = re.sub(r"\balign \d+\b", " ", sig)
    sig = re.sub(r"#[0-9]+", " ", sig)          # attribute-group references
    sig = re.sub(r"%[A-Za-z0-9._]*", " ", sig)  # SSA parameter names
    tokens = [t for t in sig.replace(",", " , ").split() if t not in ATTRS]
    return " ".join(tokens)


def _balanced(text: str, open_pos: int) -> str | None:
    """Return the text between the parenthesis at `open_pos` and its match."""
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[open_pos + 1:i]
    return None


def _split_params(params: str) -> tuple[str, ...]:
    """Split a parameter list on top-level commas, strip, and drop empties."""
    out: list[str] = []
    depth = 0
    cur = ""
    for ch in params:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    stripped = [_strip_attrs(p).strip() for p in out]
    return tuple(p for p in stripped if p)


def full_signature(text: str, keyword: str,
                   name: str) -> tuple[str, tuple[str, ...]] | None:
    """Extract `(RET, (PARAM, ...))` of @name from a define/declare line.

    Unlike the old `sig()` this covers the RETURN TYPE as well as the
    parameter list: a dropped `ptr addrspace(4)` return that both TUs share
    used to pass the caller-vs-callee equality, and a return mutated on both
    sides escapes it the same way.  The oracle comparison below is against
    the literal SIG_ORACLE table, so a shared mutation is a FAIL.
    """
    m = re.search(rf"\b{keyword}\b[^\n]*?@{re.escape(name)}\s*\(", text)
    if not m:
        return None
    inner = _balanced(text, m.end() - 1)
    if inner is None:
        return None
    head = text[:m.end() - 1]
    # Return type: from the end of the previous declaration to @name, minus
    # the calling-convention/attribute tokens between them.
    prev = max(head.rfind("\n"), head.rfind("}"))
    ret = head[prev + 1:]
    ret = re.sub(rf"\b{keyword}\b", " ", ret, count=1)
    ret = re.sub(r"@[A-Za-z0-9_]+$", "", ret.strip())
    ret = re.sub(r"\bcc \d+\b", " ", ret)
    return _strip_attrs(ret).strip(), _split_params(inner)


def check_oracle(text: str, side: str, f: Findings) -> None:
    """Every entry point must match the complete frozen oracle signature."""
    for name, (want_ret, want_params) in SIG_ORACLE.items():
        got = full_signature(text, "declare" if side == "caller" else "define",
                             name)
        if got is None:
            f.failures.append(
                f"oracle/{name}: no {side} signature found for the frozen "
                f"oracle {want_ret} ({', '.join(want_params)})")
            continue
        got_ret, got_params = got
        if got_ret != want_ret:
            f.failures.append(
                f"oracle/{name}: {side} return type {got_ret!r} != frozen "
                f"oracle {want_ret!r}")
        if got_params != want_params:
            f.failures.append(
                f"oracle/{name}: {side} parameters {got_params!r} != frozen "
                f"oracle {want_params!r}")
        # Explicit per-entry address-space requirement, independent of the
        # oracle string: the generic entry point must not carry any
        # addrspace, and the CODE entries must keep addrspace(4).
        joined = got_ret + " " + " ".join(got_params)
        if name in GENERIC_ENTRIES and re.search(r"addrspace\(\d+\)", joined):
            f.failures.append(
                f"oracle/{name}: {side} generic entry point must be AS0 "
                f"(no addrspace qualifier), got {joined!r}")
        if name in AS4_ENTRIES and "addrspace(4)" not in joined:
            f.failures.append(
                f"oracle/{name}: {side} CODE entry point lost "
                f"addrspace(4): {joined!r}")


def check_oracle_complete(f: Findings) -> None:
    """The frozen oracle must itself be complete (R7 structural rule)."""
    if set(SIG_ORACLE) != set(CALLEE_ENTRIES):
        f.failures.append(
            f"oracle: entry set {sorted(SIG_ORACLE)!r} != frozen "
            f"{sorted(CALLEE_ENTRIES)!r}")
    for name, (ret, params) in SIG_ORACLE.items():
        if not ret:
            f.failures.append(
                f"oracle/{name}: frozen oracle has no return type")
        if name == "code_base":
            if params:
                f.failures.append(
                    f"oracle/{name}: frozen oracle claims parameters {params!r}")
        elif not params:
            f.failures.append(
                f"oracle/{name}: frozen oracle has no parameter types")


def check_pair(caller: str, callee: str) -> list[str]:
    """Cross-module agreement against the independent oracle."""
    f = Findings()
    check_oracle_complete(f)
    check_impl_module(callee, f)
    check_caller_module(caller, f)

    # 1) Both sides against the frozen oracle (not merely against each other).
    check_oracle(caller, "caller", f)
    check_oracle(callee, "callee", f)

    # 2) Cross-module equality, kept as a supplementary consistency check.
    #    It is deliberately NOT the primary evidence: two modules mutated
    #    together satisfy it (Alice R7), which the oracle above catches.
    for name in SIG_ORACLE:
        cs = full_signature(caller, "declare", name)
        ds = full_signature(callee, "define", name)
        if cs is not None and ds is not None and cs != ds:
            f.failures.append(
                f"pair/{name}: caller declares ({cs!r}) but callee defines "
                f"({ds!r})")

    # 3) Every entry point present on both sides at all.
    for name in SIG_ORACLE:
        if full_signature(caller, "declare", name) is None:
            f.failures.append(f"pair/{name}: no caller declaration found")
        if full_signature(callee, "define", name) is None:
            f.failures.append(f"pair/{name}: no callee definition found")

    return f.failures


# --------------------------------------------------------------------------
# Source-level (AST) pointee-const evidence.
# --------------------------------------------------------------------------

def run_cc1(clang: str, args: list[str], src: str,
            cwd: Path | None = None):
    import subprocess
    return subprocess.run([clang, "-cc1", "-triple", "mcs251-unknown-none",
                           "-std=c11"] + args, input=src, capture_output=True,
                          text=True, cwd=str(cwd) if cwd else None)


def check_ast_const(clang: str, inputs_dir: Path, f: Findings) -> None:
    """Prove pointee const from the source AST (IR cannot carry it).

    * the header's `code_read` parameter is the implicit-const AS4 pointee;
    * a write through it (implicitly or explicitly const) is an error;
    * a write through the generic AS0 pointer is accepted;
    * `uint8 __code *` and `const uint8 __code *` print the SAME canonical
      type.
    """
    if not clang:
        f.failures.append("ast: no clang driver path supplied (--clang); the "
                          "pointee-const evidence cannot be skipped")
        return
    hdr = "-I" + str(inputs_dir)

    dump = run_cc1(clang, [hdr, "-fsyntax-only", "-ast-dump",
                           str(inputs_dir / "mcs251-as4-cross-tu.h")], "")
    if dump.returncode != 0:
        f.failures.append("ast: header failed to parse: "
                          + dump.stderr.strip()[:200])
        return
    for want in (AST_CALLEE_READ_DECL, AST_GENERIC_DECL):
        if want not in dump.stdout:
            f.failures.append(
                f"ast: header does not declare the frozen type {want!r}; "
                f"pointee const is not proven")

    # implicit const == explicit const for the CODE pointee.
    probe = (
        "typedef unsigned char uint8;\n"
        "void a(uint8 __code *p) { (void)p; }\n"
        "void b(const uint8 __code *p) { (void)p; }\n")
    p = run_cc1(clang, ["-fsyntax-only", "-ast-dump", "-"], probe)
    lines = [l for l in p.stdout.splitlines() if "ParmVarDecl" in l]
    types = [t for l in lines for t in re.findall(r"'([^']*)'", l)]
    if len(types) < 2:
        f.failures.append(f"ast: implicit/explicit const probe produced no "
                          f"types (saw {types!r})")
    elif types[0] != types[1]:
        f.failures.append(
            f"ast: `uint8 __code *` is {types[0]!r} but "
            f"`const uint8 __code *` is {types[1]!r}; the implicit const is "
            f"not equivalent to the explicit const")

    for name, (src, err) in CONST_WRITE_PROBES.items():
        p = run_cc1(clang, [hdr, "-fsyntax-only", "-"], src)
        if p.returncode == 0 or err not in p.stderr:
            f.failures.append(
                f"ast/{name}: write through the CODE pointee was not "
                f"rejected with {err!r} (rc={p.returncode}, "
                f"stderr={p.stderr.strip()[:160]!r})")

    for name, src in CONST_CLEAN_PROBES.items():
        p = run_cc1(clang, [hdr, "-fsyntax-only", "-"], src)
        if p.returncode != 0:
            f.failures.append(
                f"ast/{name}: the generic AS0 control must be writable, got "
                f"errors={p.stderr.strip()[:160]!r}")


def check_implicit_explicit_tu(clang: str, inputs_dir: Path, f: Findings,
                               tmpdir: Path) -> None:
    """Implicit-const declaration TU + explicit-const definition TU.

    Alice review R7-4: the original test's header and callee both write
    `const`, so it never showed the implicit-const spelling and the explicit
    one are the same TU-compatible type.  These two TUs are compiled
    separately from generated sources and their IR signatures must be
    identical (the checker fails if they are not).
    """
    if not clang:
        f.failures.append("tu: no clang driver path supplied (--clang); the "
                          "implicit/explicit const TU equivalence cannot be "
                          "skipped")
        return
    decl_c = (
        "typedef unsigned char uint8;\n"
        "uint8 eq_read(uint8 __code *p, unsigned i);\n"
        "extern uint8 __code eq_tab[8];\n"
        "uint8 call_it(unsigned i) { return eq_read(eq_tab, i); }\n")
    def_c = (
        "typedef unsigned char uint8;\n"
        "uint8 eq_read(const uint8 __code *p, unsigned i);\n"
        "extern uint8 __code eq_tab[8];\n"
        "uint8 __code eq_tab[8] = {1,2,3,4,5,6,7,8};\n"
        "uint8 eq_read(const uint8 __code *p, unsigned i) "
        "{ return p[i & 7u]; }\n")
    tmpdir.mkdir(parents=True, exist_ok=True)
    decl_ll = tmpdir / "implicit-const-decl.ll"
    def_ll = tmpdir / "explicit-const-def.ll"
    for path, src in ((decl_ll, decl_c), (def_ll, def_c)):
        p = run_cc1(clang, ["-emit-llvm", "-disable-llvm-passes", "-o",
                            str(path), "-"], src)
        if p.returncode != 0:
            f.failures.append(
                f"tu: compiling {path.name} failed: {p.stderr.strip()[:200]}")
            return
    decl = decl_ll.read_text()
    defn = def_ll.read_text()
    cd = full_signature(decl, "declare", "eq_read")
    dd = full_signature(defn, "define", "eq_read")
    if cd is None or dd is None:
        f.failures.append(
            f"tu: eq_read signature missing on one side "
            f"(decl={cd!r}, def={dd!r})")
    elif cd != dd:
        f.failures.append(
            f"tu: implicit-const declaration TU declares {cd!r} but the "
            f"explicit-const definition TU defines {dd!r}")
    elif "addrspace(4)" not in (cd[0] + " " + " ".join(cd[1])):
        f.failures.append(
            f"tu: eq_read signature {cd!r} lost addrspace(4)")
    for side, text in (("decl", decl), ("def", defn)):
        got = full_signature(text, "declare", "eq_tab")
        if got is None:
            m = re.search(r"@eq_tab = [^\n]*", text)
            if not m or "addrspace(4) constant" not in m.group(0):
                f.failures.append(
                    f"tu/{side}: eq_tab is not an addrspace(4) constant "
                    f"global in the {side} TU")


# --------------------------------------------------------------------------
# Self-tests: mutate the tables/inputs and require detection.
# --------------------------------------------------------------------------


def run_self_tests(caller: str, callee: str) -> list[str]:
    escaped: list[str] = []

    # The oracle itself must be complete: every entry has both a return type
    # and its frozen parameter set, otherwise an oracle stripped of its
    # return type would accept anything (M8).
    incomplete = Findings()
    check_oracle_complete(incomplete)
    if incomplete.failures:
        escaped.append("M8-baseline: the frozen oracle is incomplete: "
                       + "; ".join(incomplete.failures))
    # The oracle must be satisfiable on the real inputs, or the mutations
    # below would be vacuous.
    base = check_pair(caller, callee)
    if base:
        escaped.append("baseline pair check failed: " + "; ".join(base))

    # M1: drop const (the table becomes `global`, i.e. writable) -> FAIL.
    mutated = re.sub(r"@shared_rom = (?:dso_local )?addrspace\(4\) constant",
                     "@shared_rom = dso_local addrspace(4) global", callee)
    if not check_pair(caller, mutated):
        escaped.append("M1: dropping const from the CODE table was not detected")
    # M1b: change an expectation to drop const -> FAIL.
    save = dict(CALLEE_DEFS)
    CALLEE_DEFS["shared_rom"] = \
        r"@shared_rom = (?:dso_local )?addrspace\(4\) global \[8 x i8\]"
    if not check_pair(caller, callee):
        escaped.append("M1b: a const-less expectation was not rejected")
    CALLEE_DEFS.clear()
    CALLEE_DEFS.update(save)

    # M2: change an address space (drop addrspace(4) from code_read) -> FAIL.
    mutated = re.sub(r"define dso_local zeroext i8 @code_read\(ptr addrspace\(4\)",
                     "define dso_local zeroext i8 @code_read(ptr", callee)
    if not check_pair(caller, mutated):
        escaped.append("M2: dropping addrspace(4) from the callee was not "
                       "detected")
    # M2b: move AS4 onto the pointer object instead of the pointee.
    mutated = re.sub(r"declare (?:dso_local )?ptr addrspace\(4\) @code_lookup",
                     "declare dso_local ptr @code_lookup", caller)
    if not check_pair(mutated, callee):
        escaped.append("M2b: dropping addrspace(4) from the caller was not "
                       "detected")

    # M3: drop every addrspacecast from the caller -> FAIL.
    mutated = caller.replace("addrspacecast (ptr addrspace(4)", "")
    mutated = "\n".join(l for l in mutated.splitlines()
                        if "addrspacecast ptr addrspace(4)" not in l)
    if not check_pair(mutated, callee):
        escaped.append("M3: a caller with no addrspacecast was not detected")

    # M4: replace every conversion with a bitcast -> FAIL (both because the
    # required addrspacecast disappears and because bitcast is forbidden).
    mutated = re.sub(r"addrspacecast (?:\(\s*)?ptr addrspace\(4\)",
                     "bitcast (ptr addrspace(4)", caller)
    if not check_pair(mutated, callee):
        escaped.append("M4: a bitcast conversion was not detected")

    # M5: forget a required function on the callee side -> FAIL.  The
    # definition body is removed, so both the entry-set check and the
    # signature check must notice.
    mutated = re.sub(
        r"define [^\n]*@code_base\(\)[^\n]*\n(?:[^\n]*\n)*?\}\n", "",
        callee)
    if not check_pair(caller, mutated):
        escaped.append("M5: a missing callee definition was not detected")

    # M6: inject an AS4 store into the callee -> FAIL.
    mutated = callee.replace("ret i8 %",
                             "store i8 0, ptr addrspace(4) %p, align 1\n  ret i8 %", 1)
    if not check_pair(caller, mutated):
        escaped.append("M6: an AS4 store in the callee was not detected")

    # M7: Alice's R7 counterexample -- mutate BOTH sides' `generic_read` to
    # `ptr addrspace(3)` (an XDATA pointee).  Caller and callee agree with
    # each other, so the old equality check passed; the frozen oracle must
    # reject it.
    as3_caller = re.sub(r"(declare [^\n]*@generic_read\()ptr\b", r"\1ptr addrspace(3)",
                        caller)
    as3_callee = re.sub(r"(define [^\n]*@generic_read\()ptr\b", r"\1ptr addrspace(3)",
                        callee)
    if as3_caller == caller or as3_callee == callee:
        escaped.append("M7: could not construct the AS3 generic mutation")
    else:
        if not check_pair(as3_caller, as3_callee):
            escaped.append("M7: a caller+callee pair mutated together to "
                           "`ptr addrspace(3)` generic_read was accepted")

    # M7b: same escape on a CODE entry point -- both sides drop AS4.
    as0_caller = re.sub(r"(declare [^\n]*@code_read\()ptr addrspace\(4\)",
                        r"\1ptr", caller)
    as0_callee = re.sub(r"(define [^\n]*@code_read\()ptr addrspace\(4\)",
                        r"\1ptr", callee)
    if not check_pair(as0_caller, as0_callee):
        escaped.append("M7b: a caller+callee pair mutated together to a "
                       "generic code_read was accepted")

    # M7c: change the RETURN type on both sides together (the old `sig()`
    # only extracted parameters, so this escaped).
    ret_caller = re.sub(r"(declare [^\n]*)ptr addrspace\(4\) (@code_lookup)",
                        r"\1ptr \2", caller)
    ret_callee = re.sub(r"(define [^\n]*)ptr addrspace\(4\) (@code_lookup)",
                        r"\1ptr \2", callee)
    if ret_caller == caller or ret_callee == callee:
        escaped.append("M7c: could not construct the return-type mutation")
    else:
        if not check_pair(ret_caller, ret_callee):
            escaped.append("M7c: a caller+callee pair with a mutated RETURN "
                           "type was accepted")

    # M8: strip the return type from an oracle entry -> the oracle is
    # incomplete and must be rejected structurally.
    save_oracle = dict(SIG_ORACLE)
    SIG_ORACLE["code_lookup"] = ("", ("i32",))
    incomplete = Findings()
    check_oracle_complete(incomplete)
    if not incomplete.failures:
        escaped.append("M8: a return-type-less oracle entry was not detected")
    SIG_ORACLE.clear()
    SIG_ORACLE.update(save_oracle)

    # M8b: removing a whole oracle entry must not disable that function's
    # independent check (Alice third review R12).
    del SIG_ORACLE["generic_read"]
    if not check_pair(caller, callee):
        escaped.append("M8b: deleting an oracle entry was not detected")
    SIG_ORACLE.clear()
    SIG_ORACLE.update(save_oracle)

    # M9: weaken the AS4 oracle expectation to a plain `ptr`.  The mutated
    # oracle must no longer be satisfied by the real AS4 modules.
    save_oracle = dict(SIG_ORACLE)
    SIG_ORACLE["code_read"] = ("i8", ("ptr", "i32"))
    if not check_pair(caller, callee):
        escaped.append("M9: weakening the AS4 oracle expectation to a plain "
                       "`ptr` was accepted")
    SIG_ORACLE.clear()
    SIG_ORACLE.update(save_oracle)

    return escaped


def main() -> int:
    import os
    import tempfile
    ap = argparse.ArgumentParser()
    ap.add_argument("--caller", required=True, help="caller TU IR file")
    ap.add_argument("--callee", required=True, help="callee TU IR file")
    ap.add_argument("--clang", default="",
                    help="clang driver for the AST pointee-const and "
                         "implicit/explicit const TU checks (required, not "
                         "skippable)")
    ap.add_argument("--inputs-dir", default="",
                    help="directory holding mcs251-as4-cross-tu.h")
    ap.add_argument("--tmp-dir", default="",
                    help="scratch directory for the generated TUs "
                         "(default: a fresh system temp directory)")
    ap.add_argument("--self-test", action="store_true")
    opts = ap.parse_args()

    caller = Path(opts.caller).read_text()
    callee = Path(opts.callee).read_text()

    if opts.self_test:
        escaped = run_self_tests(caller, callee)
        if escaped:
            sys.stderr.write("check-as4-cross-tu: SELF-TEST FAIL\n")
            for e in escaped:
                sys.stderr.write("  " + e + "\n")
            return 1
        print("check-as4-cross-tu: self-tests PASS")
        return 0

    failures = check_pair(caller, callee)
    # Pointee const is a SOURCE property (opaque pointer IR cannot carry it):
    # observe it on the AST with compile-time write probes, and prove the
    # implicit-const declaration TU and explicit-const definition TU are the
    # same ABI type.  Both are mandatory, never skipped.
    f = Findings()
    inputs_dir = Path(opts.inputs_dir) if opts.inputs_dir \
        else Path(__file__).resolve().parent
    if opts.tmp_dir:
        tmpdir = Path(opts.tmp_dir)
    else:
        tmpdir = Path(tempfile.mkdtemp(prefix="check-as4-cross-tu-"))
    check_ast_const(opts.clang, inputs_dir, f)
    check_implicit_explicit_tu(opts.clang, inputs_dir, f, tmpdir)
    failures += f.failures

    if failures:
        sys.stderr.write(f"check-as4-cross-tu: FAIL ({len(failures)})\n")
        for x in failures:
            sys.stderr.write("  " + x + "\n")
        return 1
    print("check-as4-cross-tu: both TUs match the frozen signature oracle "
          "(AS4+implicit-const pointee, AS0 generic, addrspacecast at use "
          "sites); AST write probes prove the pointee const and the "
          "implicit-const TU equals the explicit-const TU")
    return 0


if __name__ == "__main__":
    sys.exit(main())
