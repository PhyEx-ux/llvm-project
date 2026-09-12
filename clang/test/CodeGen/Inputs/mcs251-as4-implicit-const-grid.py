#!/usr/bin/env python3
"""A2a cartesian-product checker for the MCS-251 `__code` implicit-const rule.

Companion checker for clang/test/CodeGen/mcs251-as4-implicit-const-grid.c
(RUNTIME-AS-PTR-DESIGN-A.md §3-A2, "A2a 验收补充").  The design freezes a
three-axis grid and forbids shrinking it:

  axis 1 qualifier combination (6):
      __code / __code const / const __code / __code volatile /
      __code const volatile / no __code (AS0 control)
  axis 2 declaration form (15):
      bare object, typedef, typedef-of-typedef, pointer pointee,
      pointer object, array, array element, function parameter,
      function parameter array, function pointer return, function value
      return, struct member, extern declaration + definition merge,
      merge with differing qualifiers, automatic storage
  axis 3 spelling (3):
      `__code`, -fmcs251-keil bare `code`, address_space(4)

The checker enumerates the *complete product* 6 x 15 x 3 = 270 cells and
validates coverage independently of the compiler: every cell of the product
must be generated exactly once and every case must carry its full axis tuple.
Deleting a cell, duplicating a tuple, or dropping an axis value is a FAIL
before any compiler runs.

Each cell additionally proves, from the compiler's own output:
  * the canonical AST type of the declared entity (positive cells),
  * the exact diagnostic set -- expected warnings/errors are a literal list,
    and any *unexpected* warning or error is a FAIL (fail-closed),
  * the emitted IR address space and const-ness of the global/load.

Positives must carry both AST and IR assertions; negatives must carry an
explicit expected error set and a `no_ir_reason` saying why IR observation
does not apply.  This is enforced structurally, so no row can silently skip
its IR or AST half (the earlier checker had 34 IR-less rows and 6 AST skips).

Every qualifier/pointer row also runs a *semantic mutation probe*: writing
through the pointee / assigning the pointer object / assigning the object
must be diagnosed exactly where the frozen const level says it must be, and
must be accepted where it says it must be.  The AS0 control rows carry the
same probes with inverted expectations.  This proves the const level after
typedef expansion and catches `char x;`-style substitutions that the sugar-
only AST string cannot see.

The axes are frozen TWICE, independently (Alice review R6): the generation
axes (`QUAL`/`FORM`/`SPELLINGS`) are compared, value by value and in order,
against literal constant tables (`FROZEN_*`), and coverage is computed from
those literal tables rather than from the live axes.  Shrinking a generation
axis (e.g. deleting `param-array` from `AXIS_FORM`) therefore cannot shrink
the acceptance set: the frozen product stays 6x15x3 = 270 cells and the
missing cells are reported before any compiler runs.

Every positive cell must name the declaration it asserts (`ast_decl`): a
missing name is a FAIL, never a silent skip, so `ast_must` alone cannot stand
in for AST observation.

Fail-closed self-tests (`--self-test`) mutate the grid and require each
mutation to be caught:
  S1 delete a cell; S2 duplicate an axis tuple; S3 drop an axis value;
  S4 drop `const` from an expectation; S5 drop `volatile` from an
  expectation; S6 replace every AS4 object row with a writable `char x;`;
  S7 inject an unexpected discarded-qualifier warning into a positive cell;
  S8 expect success for a negative cell; S9 remove a required probe;
  S10 declare an AS0 cell with `__code` but keep AS0 expectations;
  S11 shrink a *generation* axis (drop `param-array` from `AXIS_FORM`, Alice's
  R6 counterexample) -> coverage must fail against the frozen product;
  S12 blank a positive cell's `ast_decl` -> check_case must fail instead of
  silently skipping the AST check.
"""

from __future__ import annotations

import argparse
import itertools
import re
import subprocess
import sys
from dataclasses import dataclass, field, replace

CLANG = "clang"
TRIPLE = "mcs251-unknown-none"
STD = "c11"

# --------------------------------------------------------------------------
# Axis definitions.  These are the frozen axes; the product is generated from
# them and coverage is validated against them, so editing an axis without
# updating the product is impossible to miss.
# --------------------------------------------------------------------------

# axis 1: qualifier combination.  `pre` are the qualifiers written before
# `char`, `post` those written after the address-space token; `as4` says
# whether the cell carries the CODE address space at all.  The spellings are
# exactly the five combinations frozen by the design (`__code`, `__code
# const`, `const __code`, `__code volatile`, `__code const volatile`) plus the
# AS0 control.
QUAL = {
    "as4": dict(pre="", post="", as4=True),
    "as4-post-const": dict(pre="", post="const", as4=True),
    "as4-pre-const": dict(pre="const", post="", as4=True),
    "as4-post-volatile": dict(pre="", post="volatile", as4=True),
    "as4-post-cv": dict(pre="", post="const volatile", as4=True),
    "as0": dict(pre="", post="", as4=False),
}

# axis 2: declaration form.  Each form is a template over the placeholders
# {P} (pre qualifiers + space), {T} (spelling token + space), {O} (space +
# post qualifiers) and {N} (entity name).
FORM = {
    "bare": lambda p, t, o, n: f"{p}char {t}{o} {n};\n",
    "typedef": lambda p, t, o, n: f"typedef {p}char {t}{o} C;\nC {n};\n",
    "typedef2": lambda p, t, o, n:
        f"typedef {p}char {t}{o} C;\ntypedef C C2;\nC2 {n};\n",
    "pointee": lambda p, t, o, n: f"{p}char {t}{o} *{n};\n",
    "ptr-object": lambda p, t, o, n: f"{p}char * {t}{o} {n};\n",
    "array": lambda p, t, o, n: f"{p}char {t}{o} a[4];\n",
    "array-elem": lambda p, t, o, n:
        f"{p}char {t}{o} a[4];\nchar f(unsigned i) {{ return a[i]; }}\n",
    "param-ptr": lambda p, t, o, n:
        f"void f({p}char {t}{o} *p) {{ (void)*p; }}\n",
    "param-array": lambda p, t, o, n:
        f"void f({p}char {t}{o} a[]) {{ (void)*a; }}\n",
    "ret-ptr": lambda p, t, o, n:
        f"{p}char {t}{o} *f(void);\nvoid g(void) {{ (void)f(); }}\n",
    "ret-value": lambda p, t, o, n:
        f"{p}char {t}{o} f(void);\nvoid g(void) {{ (void)f(); }}\n",
    "member": lambda p, t, o, n:
        f"struct S {{ {p}char {t}{o} m; }};\n"
        f"void g(struct S *s) {{ (void)s->m; }}\n",
    "merge": lambda p, t, o, n:
        f"extern {p}char {t}{o} {n};\n{p}char {t}{o} {n};\n",
    "merge-mismatch": lambda p, t, o, n:
        f"extern {p}char {t}{o} {n};\nchar {n};\n",
    "local": lambda p, t, o, n:
        f"void f(void) {{ {p}char {t}{o} l; (void)l; }}\n",
}

# axis 3: spelling.  Key -> (token, extra command-line switch).
SPELLINGS = {
    "core": ("__code", None),
    "keil": ("code", "-fmcs251-keil"),
    "attr": ("__attribute__((address_space(4)))", None),
}

AXIS_QUAL = list(QUAL)
AXIS_FORM = list(FORM)
AXIS_SPELL = list(SPELLINGS)

# --------------------------------------------------------------------------
# Independently frozen acceptance axes (Alice review R6).  These literal
# tables are the oracle: they are NOT derived from QUAL/FORM/SPELLINGS, so
# editing a generation axis cannot shrink the acceptance set.  `FROZEN_*` is
# compared against the generation axes themselves (so a rewritten axis tuple
# is caught) and the coverage product is computed from `FROZEN_*` (so a
# deleted generation value is caught even if the axes are edited in place).
# Any edit here must be a deliberate re-freeze of the acceptance contract,
# visible in review as a literal-table change.
# --------------------------------------------------------------------------

# axis 1: the five CODE qualifier combinations plus the AS0 control, in the
# order frozen by RUNTIME-AS-PTR-DESIGN-A.md §3-A2.
FROZEN_QUAL = (
    "as4",
    "as4-post-const",
    "as4-pre-const",
    "as4-post-volatile",
    "as4-post-cv",
    "as0",
)

# axis 2: the fifteen declaration forms.
FROZEN_FORM = (
    "bare",
    "typedef",
    "typedef2",
    "pointee",
    "ptr-object",
    "array",
    "array-elem",
    "param-ptr",
    "param-array",
    "ret-ptr",
    "ret-value",
    "member",
    "merge",
    "merge-mismatch",
    "local",
)

# axis 3: the three spellings.
FROZEN_SPELL = ("core", "keil", "attr")

# The design-frozen cardinality, written out independently of the tables so a
# coordinated edit of a table and its axis is still caught.
FROZEN_COUNTS = (6, 15, 3)
FROZEN_PRODUCT = 270

# Entity names per form (the name probed in AST dumps).
FORM_NAME = {
    "bare": "x", "typedef": "v", "typedef2": "v", "pointee": "p",
    "ptr-object": "p", "array": "a", "array-elem": "a", "param-ptr": "p",
    "param-array": "a", "ret-ptr": "f", "ret-value": "f", "member": "m",
    "merge": "e", "merge-mismatch": "e", "local": "l",
}

# Forms that are rejected outright for a CODE-qualified declaration: struct
# members, value returns, mismatched merges and automatic storage.
FORMS_REJECT_AS4 = {"ret-value", "member", "merge-mismatch", "local"}

# Forms whose const level must be proven by a mutation probe.  The value is
# the list of probe templates; each probe is compiled alone and must be
# diagnosed (CODE) or accepted (AS0 control).
PROBE_FORMS = {
    "bare": [("object-write", "{p}char {t}{o} x;\nvoid g(void) {{ x = 1; }}\n")],
    "typedef": [("typedef-write",
                 "typedef {p}char {t}{o} C;\nC v;\nvoid g(void) {{ v = 1; }}\n")],
    "typedef2": [("typedef2-write",
                  "typedef {p}char {t}{o} C;\ntypedef C C2;\nC2 v;\n"
                  "void g(void) {{ v = 1; }}\n")],
    "pointee": [("pointee-write", "{p}char {t}{o} *p;\nvoid g(void) {{ *p = 1; }}\n"),
                ("ptrobj-assign", "{p}char {t}{o} *p;\nvoid g(void) {{ p = 0; }}\n")],
    "ptr-object": [("ping-write", "{p}char * {t}{o} p;\nvoid g(void) {{ *p = 1; }}\n"),
                   ("pobj-assign", "{p}char * {t}{o} p;\nvoid g(void) {{ p = 0; }}\n")],
    "array": [("array-write", "{p}char {t}{o} a[4];\nvoid g(void) {{ a[0] = 1; }}\n")],
    "array-elem": [("elem-write",
                    "{p}char {t}{o} a[4];\nvoid g(void) {{ a[0] = 1; }}\n")],
    "param-ptr": [("param-write",
                   "void f({p}char {t}{o} *p) {{ *p = 1; }}\n")],
    "param-array": [("param-elem-write",
                     "void f({p}char {t}{o} a[]) {{ a[0] = 1; }}\n")],
    "ret-ptr": [("ret-elem-write",
                 "{p}char {t}{o} *f(void);\nvoid g(void) {{ f()[0] = 1; }}\n")],
    "merge": [("merge-write",
               "extern {p}char {t}{o} e;\n{p}char {t}{o} e;\n"
               "void g(void) {{ e = 1; }}\n")],
}

ERR_READONLY = "read-only variable is not assignable"
ERR_CONST_VAR = "cannot assign to variable"
ERR_DIFF_AS = "changes address space of pointer"


def canon_type(s: str) -> str:
    """Canonicalize a printed clang type string for comparison.

    The three spellings go through different type-construction paths, and the
    `address_space(4)` attribute spelling prints repeated/ordered qualifiers
    from both the attributed sugar and the modified type (`const const ...`,
    `volatile const ...`).  Those are printer artifacts of the same CVR set,
    not semantic differences, so collapse duplicated qualifiers and fix the
    const/volatile order.  A *dropped* qualifier still changes the string, so
    the mutation self-tests (S4/S5/S10) keep failing.
    """
    def repl(m):
        qs = sorted(set(re.findall(r"const|volatile", m.group(0))))
        order = [q for q in ("const", "volatile") if q in qs]
        return " ".join(order) + " "

    s = re.sub(r"(?:(?:const|volatile)\s+)+", repl, s)
    return " ".join(s.split())

# Expected rejection class per (form, probe direction) for an AS4 cell.
# "readonly" -> the pointee/element is read-only; "constvar" -> the object or
# pointer object is const; None -> the probe must be accepted.
AS4_PROBE_EXPECT = {
    "bare": {"object-write": "constvar"},
    "typedef": {"typedef-write": "constvar"},
    "typedef2": {"typedef2-write": "constvar"},
    "pointee": {"pointee-write": "readonly", "ptrobj-assign": None},
    "ptr-object": {"ping-write": None, "pobj-assign": "constvar"},
    "array": {"array-write": "constvar"},
    "array-elem": {"elem-write": "constvar"},
    "param-ptr": {"param-write": "readonly"},
    "param-array": {"param-elem-write": "readonly"},
    "ret-ptr": {"ret-elem-write": "readonly"},
    "merge": {"merge-write": "constvar"},
}
# Qualifier-specific overrides: a prefix `const` moves the read-only property
# onto the pointee of a pointer-object cell as well.
AS4_PROBE_OVERRIDE = {
    ("ptr-object", "as4-pre-const"): {"ping-write": "readonly",
                                      "pobj-assign": "constvar"},
}


def _tokens(qual: str) -> dict:
    return QUAL[qual]


def _formatted(qual: str) -> tuple[str, str, str]:
    """Return (pre+space, spelling placeholder, space+post) for a cell."""
    q = QUAL[qual]
    pre = (q["pre"] + " ") if q["pre"] else ""
    post = (" " + q["post"]) if q["post"] else ""
    return pre, post


def _token(spell: str, qual: str) -> str:
    if not QUAL[qual]["as4"]:
        return ""
    return SPELLINGS[spell][0] + " "


def _render(form: str, qual: str, spell: str) -> str:
    pre, post = _formatted(qual)
    return FORM[form](pre, _token(spell, qual), post, FORM_NAME[form])


# --------------------------------------------------------------------------
# Canonical type strings.  The rule under test: for an AS4 entity the implicit
# `const` lands on the same entity as the address space; explicit prefix
# qualifiers always qualify the pointee (or the declared object type), and
# explicit postfix qualifiers qualify the entity carrying the address space.
# --------------------------------------------------------------------------


def _quals(pre: str, post: str) -> list[str]:
    s = pre + " " + post
    out = []
    if "const" in s:
        out.append("const")
    if "volatile" in s:
        out.append("volatile")
    return out

def ctype(pre: str, post: str, as4: bool, base: str = "char") -> str:
    """Canonical type of an object/pointee carrying the qualifiers.

    The implicit `const` of a CODE entity belongs to the same CVR set as the
    explicit qualifiers, so it must be printed on the entity that carries the
    address space (clang orders the set as const, volatile).
    """
    parts = _quals(pre, post)
    if as4 and "const" not in parts:
        parts = ["const"] + parts
    s = (" ".join(parts) + " ") if parts else ""
    if as4:
        s += "__attribute__((address_space(4))) "
    return s + base


def ptr_object_type(qual: str) -> str:
    """Canonical type of `char * __code p` (address space on the pointer)."""
    q = QUAL[qual]
    pointee = _quals(q["pre"], "")
    obj = _quals("", q["post"])
    if q["as4"] and "const" not in obj:
        obj = ["const"] + obj
    ps = (" ".join(pointee) + " ") if pointee else ""
    os_ = (" ".join(obj) + " ") if obj else ""
    s = ps + "char *" + os_
    if q["as4"]:
        s += "__attribute__((address_space(4)))"
    return s


def entity_type(form: str, qual: str) -> str:
    """Canonical type printed for the entity of a positive cell."""
    q = QUAL[qual]
    if form == "ptr-object":
        return ptr_object_type(qual)
    if form in ("array", "array-elem"):
        return ctype(q["pre"], q["post"], q["as4"], base="char[4]")
    if form in ("pointee", "param-ptr", "param-array"):
        return ctype(q["pre"], q["post"], q["as4"]) + " *"
    if form == "ret-ptr":
        return ctype(q["pre"], q["post"], q["as4"]) + " *(void)"
    if form == "ret-value":
        return ctype(q["pre"], q["post"], q["as4"]) + " (void)"
    return ctype(q["pre"], q["post"], q["as4"])


# --------------------------------------------------------------------------
# Case model.
# --------------------------------------------------------------------------


@dataclass
class Probe:
    name: str
    source: str
    errors: list[str] = field(default_factory=list)  # empty = must be clean


@dataclass
class Case:
    name: str
    source: str
    axes: tuple[str, str, str] | None = None
    group: str = "product"  # "product" or the extra-group tag
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    ast_decl: str | None = None
    ast_must: list[str] = field(default_factory=list)
    ast_must_not: list[str] = field(default_factory=list)
    ir_must: list[str] = field(default_factory=list)
    ir_must_not: list[str] = field(default_factory=list)
    probes: list[Probe] = field(default_factory=list)
    no_ir_reason: str = ""

    @property
    def is_negative(self) -> bool:
        return bool(self.errors)


# --------------------------------------------------------------------------
# Product generation.
# --------------------------------------------------------------------------


def _ir_for(form: str, qual: str) -> tuple[list[str], list[str]]:
    """Return (must, must_not) IR regexes for a positive product cell.

    `must_not` patterns are anchored to the entity under test, never a bare
    `addrspace(4)`: the function itself lives in the program address space
    (AS4 by default), so a blanket scan would be vacuous or wrong.
    """
    q = QUAL[qual]
    as4 = q["as4"]
    name = FORM_NAME[form]
    must: list[str] = []
    must_not: list[str] = []

    if form in ("bare", "typedef", "typedef2", "merge"):
        must.append(rf"@{name} = (?:dso_local )?addrspace\(4\) constant i8 0"
                    if as4 else rf"@{name} = (?:dso_local )?global i8 0")
        if as4:
            must_not.append(rf"@{name} = (?:dso_local )?global i8")
        else:
            must_not.append(rf"@{name} = (?:dso_local )?addrspace\(4\)")
    elif form == "array":
        must.append(rf"@{name} = (?:dso_local )?addrspace\(4\) constant \[4 x i8\]"
                    if as4
                    else rf"@{name} = (?:dso_local )?global \[4 x i8\]")
        must_not.append(rf"@{name} = (?:dso_local )?global \[4 x i8\]" if as4
                        else rf"@{name} = (?:dso_local )?addrspace\(4\)")
    elif form == "array-elem":
        must.append(r"@a = (?:dso_local )?addrspace\(4\) constant \[4 x i8\]"
                    if as4 else r"@a = (?:dso_local )?global \[4 x i8\]")
        must.append(r"load (?:volatile )?i8, ptr addrspace\(4\)" if as4
                    else r"load (?:volatile )?i8, ptr %")
        must_not.append(r"@a = (?:dso_local )?global \[4 x i8\]" if as4
                        else r"@a = (?:dso_local )?addrspace\(4\)")
    elif form == "pointee":
        must.append(rf"@{name} = (?:dso_local )?global ptr addrspace\(4\) null"
                    if as4 else rf"@{name} = (?:dso_local )?global ptr null")
        must_not.append(rf"@{name} = (?:dso_local )?addrspace\(4\) constant ptr"
                        if as4 else rf"@{name} = (?:dso_local )?global ptr addrspace\(4\)")
    elif form == "ptr-object":
        must.append(rf"@{name} = (?:dso_local )?addrspace\(4\) constant ptr null"
                    if as4 else rf"@{name} = (?:dso_local )?global ptr null")
        must_not.append(rf"@{name} = (?:dso_local )?global ptr null" if as4
                        else rf"@{name} = (?:dso_local )?addrspace\(4\)")
    elif form in ("param-ptr", "param-array"):
        arg = "%p" if form == "param-ptr" else "%a"
        # The parameter object itself always lives in AS4 (program AS); the
        # pointee's space is what the qualifier controls.
        if as4:
            must.append(rf"@f\(ptr addrspace\(4\) noundef {arg}\)")
            must.append(r"load (?:volatile )?i8, ptr addrspace\(4\)")
        else:
            must.append(rf"@f\(ptr noundef {arg}\)")
            must.append(r"load (?:volatile )?i8, ptr %")
            must_not.append(rf"@f\(ptr addrspace\(4\) noundef {arg}\)")
    elif form == "ret-ptr":
        # The declaration is emitted at its first use (`g` calls it).
        must.append(r"declare (?:nonnull )?ptr addrspace\(4\) @f\(\)" if as4
                    else r"declare (?:nonnull )?ptr @f\(\)")
        must_not.append(r"declare (?:nonnull )?ptr @f\(\)" if as4
                        else r"declare (?:nonnull )?ptr addrspace\(4\) @f\(\)")
    elif form == "member":
        # AS0 only (the AS4 cells are rejected in Sema).  The member access
        # must stay an AS0 load of the struct field.
        must.append(r"load (?:volatile )?i8, ptr %")
        must_not.append(r"load (?:volatile )?i8, ptr addrspace\(4\)")
    elif form == "local":
        # AS0 only (the AS4 cells are rejected in Sema).  Automatic storage is
        # a plain AS0 alloca, never an address-space-qualified object.
        must.append(r"alloca i8")
        must_not.append(r"addrspace\(4\) constant")
    elif form == "ret-value":
        # AS0 only (the AS4 cells are rejected in Sema).  The value return is
        # a plain scalar declaration, never an AS4-qualified pointer type.
        must.append(r"declare signext i8 @f\(\)")
        must_not.append(r"ptr addrspace\(4\) @f\(\)")
    elif form == "merge-mismatch":
        # AS0 only (the AS4 cells are rejected in Sema).  The plain object is
        # an ordinary writable global in AS0.
        must.append(r"@e = (?:dso_local )?global i8 0")
        must_not.append(r"@e = (?:dso_local )?addrspace\(4\)")
    return must, must_not


def _probes_for(form: str, qual: str, spell: str) -> list[Probe]:
    if form not in PROBE_FORMS:
        return []
    q = QUAL[qual]
    pre = (q["pre"] + " ") if q["pre"] else ""
    post = (" " + q["post"]) if q["post"] else ""
    tok = _token(spell, qual)
    override = AS4_PROBE_OVERRIDE.get((form, qual), {})
    probes = []
    for pname, tmpl in PROBE_FORMS[form]:
        src = tmpl.format(p=pre, t=tok, o=post)
        errs: list[str] = []
        if q["as4"]:
            want = override.get(pname, AS4_PROBE_EXPECT[form][pname])
            if want == "readonly":
                errs = [ERR_READONLY]
            elif want == "constvar":
                errs = [ERR_CONST_VAR]
        probes.append(Probe(name=f"{form}/{qual}/{spell}/{pname}", source=src,
                            errors=errs))
    return probes


NEGATIVE_ERROR = {
    "ret-value": "function return type may not be qualified with an MCS251 "
                 "'__xdata' or '__code' address space",
    "member": "field may not be qualified with an address space",
    "merge-mismatch": "redefinition of 'e' with a different type",
    "local": "automatic variable qualified with an address space",
}
NO_IR_REASON = {
    "ret-value": "the declaration is rejected in Sema, so no module is emitted",
    "member": "the field declaration is rejected in Sema, no module is emitted",
    "merge-mismatch": "the redefinition is rejected in Sema, no module is emitted",
    "local": "the automatic declaration is rejected in Sema, no module is emitted",
}


def make_product_case(qual: str, form: str, spell: str) -> Case:
    src = _render(form, qual, spell)
    name = f"{form}/{qual}/{spell}"
    if QUAL[qual]["as4"] and form in FORMS_REJECT_AS4:
        return Case(name=name, source=src, axes=(qual, form, spell),
                    errors=[NEGATIVE_ERROR[form]],
                    no_ir_reason=NO_IR_REASON[form])
    must, must_not = _ir_for(form, qual)
    ast = entity_type(form, qual)
    ast_must_not = []
    if "volatile" not in (QUAL[qual]["pre"] + QUAL[qual]["post"]):
        ast_must_not.append("volatile")
    if not QUAL[qual]["as4"]:
        ast_must_not.append("address_space")
    return Case(name=name, source=src, axes=(qual, form, spell),
                ast_decl=FORM_NAME[form], ast_must=[ast],
                ast_must_not=ast_must_not,
                ir_must=must, ir_must_not=must_not,
                probes=_probes_for(form, qual, spell))


def product_cells() -> set[tuple[str, str, str]]:
    """The frozen acceptance product, computed from the literal oracle.

    Deliberately does NOT read AXIS_QUAL/AXIS_FORM/AXIS_SPELL: coverage must
    not be able to shrink when a generation axis is edited (R6).
    """
    return set(itertools.product(FROZEN_QUAL, FROZEN_FORM, FROZEN_SPELL))


def frozen_axis_failures() -> list[str]:
    """Compare the generation axes against the literal frozen tables.

    Two different escapes are covered:
      * a generation axis that lost a value (or gained one) is a mismatch
        against the frozen table even before coverage is computed;
      * a coordinated edit (table and axis changed together) is caught by the
        independently written FROZEN_COUNTS/FROZEN_PRODUCT cardinalities.
    """
    fails: list[str] = []
    for label, live, frozen in (
            ("AXIS_QUAL", tuple(AXIS_QUAL), FROZEN_QUAL),
            ("AXIS_FORM", tuple(AXIS_FORM), FROZEN_FORM),
            ("AXIS_SPELL", tuple(AXIS_SPELL), FROZEN_SPELL)):
        if live != frozen:
            missing = [v for v in frozen if v not in live]
            extra = [v for v in live if v not in frozen]
            fails.append(
                f"frozen-axis: {label} {live!r} != frozen {frozen!r} "
                f"(missing {missing!r}, extra {extra!r})")
    counts = (len(FROZEN_QUAL), len(FROZEN_FORM), len(FROZEN_SPELL))
    if counts != FROZEN_COUNTS:
        fails.append(
            f"frozen-axis: frozen cardinality {counts} != the design-frozen "
            f"{FROZEN_COUNTS}; the axis tables were edited together with the "
            f"axes they are supposed to pin")
    if len(product_cells()) != FROZEN_PRODUCT:
        fails.append(
            f"frozen-axis: product has {len(product_cells())} cells, expected "
            f"{FROZEN_PRODUCT}")
    return fails


def build_product() -> list[Case]:
    return [make_product_case(q, f, s)
            for q, f, s in itertools.product(AXIS_QUAL, AXIS_FORM, AXIS_SPELL)]


# --------------------------------------------------------------------------
# Extra grid groups required by the design (A2a "追加格点") and the AS0
# explicit-qualifier controls.
# --------------------------------------------------------------------------


def build_extras() -> list[Case]:
    cases: list[Case] = []

    # ---- group: typedef expansion const level (design extra 1) -----------
    # `CP`/`C` are sugar in the AST; the const level after expansion is proven
    # by the mutation probes plus a plain-typedef control that must accept the
    # same mutation (so the probe itself cannot be vacuous).
    cases.append(Case(
        name="typedef-expansion/pointee-const",
        group="typedef-expansion",
        source="typedef char __code CodeChar;\ntypedef CodeChar *CodeCharPtr;\n"
               "CodeCharPtr p;\nchar rd(void) { return *p; }\n",
        ast_decl="p", ast_must=["CodeCharPtr", "CodeChar *"],
        ir_must=[r"load (?:volatile )?i8, ptr addrspace\(4\)"],
        probes=[
            Probe("typedef-expansion/pointee-write",
                  "typedef char __code CodeChar;\n"
                  "typedef CodeChar *CodeCharPtr;\nCodeCharPtr p;\n"
                  "void g(void) { *p = 1; }\n", [ERR_READONLY]),
            Probe("typedef-expansion/pointee-obj-assign",
                  "typedef char __code CodeChar;\n"
                  "typedef CodeChar *CodeCharPtr;\nCodeCharPtr p;\n"
                  "void g(void) { p = 0; }\n", []),
        ]))
    cases.append(Case(
        name="typedef-expansion/pointer-object-const",
        group="typedef-expansion",
        source="typedef char *CodePtr;\nCodePtr __code p;\n",
        ast_decl="p",
        ast_must=["char *const __attribute__((address_space(4)))"],
        ir_must=[r"@p = (?:dso_local )?addrspace\(4\) constant ptr null"],
        probes=[
            Probe("typedef-expansion/pointer-object-assign",
                  "typedef char *CodePtr;\nCodePtr __code p;\n"
                  "void g(void) { p = 0; }\n", [ERR_CONST_VAR]),
            Probe("typedef-expansion/pointer-object-write",
                  "typedef char *CodePtr;\nCodePtr __code p;\n"
                  "void g(void) { *p = 1; }\n", []),
        ]))
    # Control: the same probes on a plain typedef must be accepted, proving
    # the probe detects a lost implicit const rather than rejecting writes
    # unconditionally.
    cases.append(Case(
        name="typedef-expansion/control-plain",
        group="typedef-expansion",
        source="typedef char PlainChar;\ntypedef PlainChar *PlainCharPtr;\n"
               "PlainCharPtr p;\n",
        ast_decl="p", ast_must=["PlainCharPtr", "PlainChar *"],
        ir_must=[r"@p = (?:dso_local )?global ptr null"],
        ir_must_not=[r"addrspace\(4\)"],
        probes=[
            Probe("typedef-expansion/control-pointee-write",
                  "typedef char PlainChar;\ntypedef PlainChar *PlainCharPtr;\n"
                  "PlainCharPtr p;\nvoid g(void) { *p = 1; }\n", []),
            Probe("typedef-expansion/control-object-assign",
                  "typedef char *PlainPtr;\nPlainPtr p;\n"
                  "void g(void) { p = 0; }\n", []),
        ]))

    # ---- group: duplicate / conflicting explicit qualifiers (extra 2) ----
    cases.append(Case(
        name="dup-qualifiers/identical-addrspace",
        group="dup-qualifiers",
        source="char __code __code x;\n",
        warnings=["multiple identical address spaces specified"],
        ast_decl="x",
        ast_must=["const __attribute__((address_space(4))) char"],
        ir_must=[r"@x = (?:dso_local )?addrspace\(4\) constant i8 0"],
    ))
    cases.append(Case(
        name="dup-qualifiers/duplicate-const-postfix",
        group="dup-qualifiers",
        source="char __code const const x;\n",
        warnings=["duplicate 'const' declaration specifier"],
        ast_decl="x",
        ast_must=["const __attribute__((address_space(4))) char"],
        ir_must=[r"@x = (?:dso_local )?addrspace\(4\) constant i8 0"],
    ))
    cases.append(Case(
        name="dup-qualifiers/duplicate-const-prefix",
        group="dup-qualifiers",
        source="const char __code const x;\n",
        warnings=["duplicate 'const' declaration specifier"],
        ast_decl="x",
        ast_must=["const __attribute__((address_space(4))) char"],
        ir_must=[r"@x = (?:dso_local )?addrspace\(4\) constant i8 0"],
    ))
    cases.append(Case(
        name="dup-qualifiers/typedef-const-plus-explicit",
        group="dup-qualifiers",
        source="typedef const char __code C;\nconst C x;\n",
        ast_decl="x",
        ast_must=["const __attribute__((address_space(4))) char"],
        ir_must=[r"@x = (?:dso_local )?addrspace\(4\) constant i8 0"],
    ))
    cases.append(Case(
        name="dup-qualifiers/typedef-const-only",
        group="dup-qualifiers",
        source="typedef const char __code C;\nC x;\n",
        ast_decl="x",
        ast_must=["const __attribute__((address_space(4))) char"],
        ir_must=[r"@x = (?:dso_local )?addrspace\(4\) constant i8 0"],
    ))
    cases.append(Case(
        name="dup-qualifiers/volatile-mismatch",
        group="dup-qualifiers",
        source="extern char __code e;\nchar __code volatile e;\n",
        errors=["redefinition of 'e' with a different type"],
        no_ir_reason="the redefinition is rejected in Sema, so no module is "
                     "emitted",
    ))
    # AS0 explicit-qualifier controls: dropping const/volatile here must be
    # caught by the same machinery that guards the AS4 rows.
    cases.append(Case(
        name="dup-qualifiers/as0-explicit-const",
        group="dup-qualifiers",
        source="const char x;\n",
        ast_decl="x", ast_must=["const char"],
        ir_must=[r"@x = (?:dso_local )?constant i8 0"],
        ir_must_not=[r"addrspace\(4\)"],
        probes=[Probe("dup-qualifiers/as0-const-write",
                      "const char x;\nvoid g(void) { x = 1; }\n",
                      [ERR_CONST_VAR])],
    ))
    cases.append(Case(
        name="dup-qualifiers/as0-explicit-volatile",
        group="dup-qualifiers",
        source="volatile char x;\n",
        ast_decl="x", ast_must=["volatile char"],
        ir_must=[r"@x = (?:dso_local )?global i8 0"],
        ir_must_not=[r"addrspace\(4\)"],
        probes=[Probe("dup-qualifiers/as0-volatile-write",
                      "volatile char x;\nvoid g(void) { x = 1; }\n", [])],
    ))

    # ---- group: function types get no object const (extra 3) -------------
    cases.append(Case(
        name="functype/attr-on-function-typedef",
        group="functype",
        source="typedef char F(void);\nF __code bad;\n",
        errors=["function type may not be qualified with an address space"],
        no_ir_reason="the declaration is rejected in Sema, so no module is "
                     "emitted",
    ))
    cases.append(Case(
        name="functype/value-return-qualified",
        group="functype",
        source="char __code f(void);\n",
        errors=["function return type may not be qualified with an MCS251 "
                "'__xdata' or '__code' address space"],
        no_ir_reason="the declaration is rejected in Sema, so no module is "
                     "emitted",
    ))
    cases.append(Case(
        name="functype/plain-pointer-ok",
        group="functype",
        source="typedef char F(void);\nF *fp;\nchar __code *p4;\n"
               "void use(void) { (void)fp; (void)p4; }\n",
        ast_decl="fp", ast_must=["F *"],
        ast_must_not=["address_space", "const"],
        # A function pointer is a value pointer in the *program* space (AS4),
        # but its pointee is a function type: no object const may appear and
        # the pointee is never an object pointer.
        ir_must=[r"@fp = (?:dso_local )?global ptr addrspace\(4\) null"],
        ir_must_not=[r"@fp = (?:dso_local )?addrspace\(4\) constant"],
    ))
    # A function returning a pointer into CODE keeps the AS4 on the pointee,
    # never as an object const on the function type.
    cases.append(Case(
        name="functype/ret-ptr-into-code",
        group="functype",
        source="char __code *f(void);\nvoid g(void) { (void)f(); }\n",
        ast_decl="f",
        ast_must=["const __attribute__((address_space(4))) char *(void)"],
        ast_must_not=["volatile"],
        ir_must=[r"declare (?:nonnull )?ptr addrspace\(4\) @f\(\)"],
    ))

    return cases


# --------------------------------------------------------------------------
# Parsing and checking.
# --------------------------------------------------------------------------

DIAG_RE = re.compile(r": (error|warning|note): (.*)$")


def _diagnostics(text: str) -> tuple[list[str], list[str]]:
    errors, warnings = [], []
    for line in text.splitlines():
        m = DIAG_RE.search(line)
        if not m:
            continue
        if m.group(1) == "error":
            errors.append(m.group(2))
        elif m.group(1) == "warning":
            warnings.append(m.group(2))
    return errors, warnings


def _match_set(kind: str, observed: list[str], expected: list[str],
               name: str, out: list[str]) -> None:
    """Fail-closed set comparison: neither side may contain extras."""
    for o in observed:
        if not any(e in o for e in expected):
            out.append(f"{name}: unexpected {kind}: {o!r} "
                       f"(expected {expected!r})")
    for e in expected:
        if not any(e in o for o in observed):
            out.append(f"{name}: missing expected {kind} {e!r} "
                       f"(saw {observed!r})")


def run_cc1(args: list[str], src: str) -> subprocess.CompletedProcess:
    return subprocess.run([CLANG, "-cc1", "-triple", TRIPLE, "-std=" + STD] +
                          args, input=src, capture_output=True, text=True)


def _spell_args(spell: str) -> list[str]:
    extra = SPELLINGS[spell][1]
    return [extra] if extra else []


def check_case(c: Case) -> list[str]:
    fails: list[str] = []

    # Structural fail-closed rules: no positive cell may skip AST or IR, no
    # negative cell may skip its rationale.
    if c.is_negative:
        if c.ir_must or c.ir_must_not:
            fails.append(f"{c.name}: negative case must not assert IR")
        if not c.no_ir_reason:
            fails.append(f"{c.name}: negative case lacks no_ir_reason")
        if c.ast_must:
            fails.append(f"{c.name}: negative case must not assert AST type")
    else:
        if not c.ast_must:
            fails.append(f"{c.name}: positive case lacks AST assertions")
        if not c.ir_must:
            fails.append(f"{c.name}: positive case lacks IR assertions")
        # An AST expectation with no declaration to look it up on would be a
        # silent skip: `ast_must` could never be violated (Alice review R6,
        # the `ast_decl = None` escape).  The declaration name is mandatory.
        if not c.ast_decl:
            fails.append(f"{c.name}: positive case lacks ast_decl; an "
                         f"`ast_must` with no declaration cannot be checked")
        elif not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", c.ast_decl):
            fails.append(f"{c.name}: ast_decl {c.ast_decl!r} is not a plain "
                         f"identifier and can never match a decl line")

    spell = c.axes[2] if c.axes else "core"

    # --- diagnostics + AST in one invocation (ast-dump implies syntax-only)
    p = run_cc1(["-fsyntax-only", "-ast-dump", "-"] + _spell_args(spell),
                c.source)
    diag = p.stderr
    obs_err, obs_warn = _diagnostics(diag)
    _match_set("error", obs_err, c.errors, c.name, fails)
    _match_set("warning", obs_warn, c.warnings, c.name, fails)
    if c.errors and p.returncode == 0:
        fails.append(f"{c.name}: expected a failing compile, got success")
    if not c.errors and p.returncode != 0:
        fails.append(f"{c.name}: unexpected compile failure: "
                     f"{[e for e in obs_err]}")

    # --- AST assertions
    if not c.errors and c.ast_decl and c.ast_must:
        line = None
        # The entity name immediately precedes the quoted type.  Searching
        # the whole line also matched fake names such as `const`/`char` in
        # the type itself (Alice third review R11).
        pat = re.compile(rf"\b{re.escape(c.ast_decl)} '")
        for l in p.stdout.splitlines():
            if re.search(r"(VarDecl|ParmVarDecl|FunctionDecl|FieldDecl)", l) \
                    and pat.search(l):
                line = l
        if line is None:
            fails.append(f"{c.name}: no decl line for {c.ast_decl} in AST dump")
        else:
            # Exact match against a quoted type segment (clang prints the
            # sugar first and the canonical/desugared type last when they
            # differ); a synthetic type string is accepted only for the
            # typedef-expansion rows, which are compared by their desugared
            # half.  Canonicalization collapses printer artifacts of repeated
            # qualifiers, but a *dropped* qualifier still fails.
            quoted = re.findall(r"'([^']*)'", line)
            pool = [canon_type(x) for x in quoted]
            for want in c.ast_must:
                if canon_type(want) not in pool:
                    fails.append(f"{c.name}: AST for {c.ast_decl} has "
                                 f"{quoted!r}, none equals {want!r} "
                                 f"(line {line.strip()!r})")
            for bad in c.ast_must_not:
                if re.search(rf"(?<![A-Za-z_]){re.escape(bad)}(?![A-Za-z_])",
                             line):
                    fails.append(f"{c.name}: AST for {c.ast_decl} has "
                                 f"forbidden {bad!r} in {line.strip()!r}")

    # --- IR assertions
    if not c.errors and c.ir_must:
        q = run_cc1(["-emit-llvm", "-disable-llvm-passes", "-o", "-", "-"] +
                    _spell_args(spell), c.source)
        if q.returncode != 0:
            fails.append(f"{c.name}: emit-llvm failed: {q.stderr.strip()[:200]}")
        else:
            for want in c.ir_must:
                if not re.search(want, q.stdout):
                    fails.append(f"{c.name}: IR missing {want!r}")
            for bad in c.ir_must_not:
                if re.search(bad, q.stdout):
                    fails.append(f"{c.name}: IR has forbidden {bad!r}")

    # --- semantic mutation probes
    for probe in c.probes:
        pp = run_cc1(["-fsyntax-only", "-"] + _spell_args(spell), probe.source)
        pe, pw = _diagnostics(pp.stderr)
        if probe.errors:
            if pp.returncode == 0:
                fails.append(f"{probe.name}: mutation probe was accepted, "
                             f"expected {probe.errors!r}")
            _match_set("error", pe, probe.errors, probe.name, fails)
        else:
            if pp.returncode != 0 or pe or pw:
                fails.append(f"{probe.name}: mutation probe must be clean, got "
                             f"errors={pe!r} warnings={pw!r}")

    return fails


def check_cases(cases: list[Case]) -> list[str]:
    fails: list[str] = []
    for c in cases:
        fails += check_case(c)
    return fails


# --------------------------------------------------------------------------
# Coverage validation (independent of the compiler).
# --------------------------------------------------------------------------


def validate_coverage(cases: list[Case]) -> list[str]:
    fails: list[str] = []
    fails += frozen_axis_failures()
    expected = product_cells()
    seen: dict[tuple[str, str, str], str] = {}
    for c in cases:
        if c.group != "product":
            continue
        if c.axes is None:
            fails.append(f"{c.name}: product case lacks an axis tuple")
            continue
        if c.axes in seen:
            fails.append(f"{c.name}: duplicate axis tuple {c.axes} "
                         f"(also {seen[c.axes]})")
        seen[c.axes] = c.name
    missing = expected - set(seen)
    extra = set(seen) - expected
    if missing:
        fails.append(f"coverage: missing {len(missing)} product cells: "
                     f"{sorted(missing)[:6]}...")
    if extra:
        fails.append(f"coverage: {len(extra)} cells outside the frozen "
                     f"product: {sorted(extra)[:6]}...")
    # Every probe-required form must carry at least one probe per cell.
    for c in cases:
        if c.group != "product" or c.axes is None:
            continue
        form = c.axes[1]
        if form in PROBE_FORMS and not c.probes:
            fails.append(f"{c.name}: form {form} requires mutation probes")
    # Extra groups must exist with the required content.
    expected_groups = {
        "typedef-expansion": 3,  # pointee level, pointer-object level, control
        "dup-qualifiers": 8,
        "functype": 4,
    }
    got: dict[str, int] = {}
    for c in cases:
        if c.group != "product":
            got[c.group] = got.get(c.group, 0) + 1
    for g, n in expected_groups.items():
        if got.get(g, 0) != n:
            fails.append(f"coverage: extra group {g} has {got.get(g, 0)} cases,"
                         f" expected {n}")
    unknown = set(got) - set(expected_groups)
    if unknown:
        fails.append(f"coverage: unknown extra groups {sorted(unknown)}")
    return fails


# --------------------------------------------------------------------------
# Self-tests: every mutation below must be caught.
# --------------------------------------------------------------------------


def _probe_case(cases: list[Case], name: str) -> Case | None:
    for c in cases:
        if c.name == name:
            return c
    return None


def run_self_tests() -> list[str]:
    """Return a list of escaped mutations (empty means all caught)."""
    escaped: list[str] = []
    cases = build_product() + build_extras()

    # Baseline: the real grid must satisfy coverage.
    base_fails = validate_coverage(cases)
    if base_fails:
        escaped.append("baseline coverage failed: " + "; ".join(base_fails))

    # S1: delete one grid cell -> coverage must fail.
    mutant = [c for c in cases if c.name != "bare/as4/core"]
    if not validate_coverage(mutant):
        escaped.append("S1: deleting a grid cell was not detected")

    # S2: duplicate an axis tuple under a new name -> coverage must fail.
    dup = replace(_probe_case(cases, "bare/as4/core"), name="bare/as4/core-copy")
    if not validate_coverage(cases + [dup]):
        escaped.append("S2: duplicated axis tuple was not detected")

    # S3: drop an axis value (all array/as0/keil cells) -> coverage must fail.
    mutant = [c for c in cases
              if (c.axes or ("", "", ""))[:2] != ("as0", "array")
              or (c.axes or ("", "", ""))[2] != "keil"]
    if not validate_coverage(mutant):
        escaped.append("S3: dropping an axis value was not detected")

    # S4: drop `const` from an expectation -> check_case must fail.
    c = _probe_case(cases, "bare/as4/core")
    mutant_case = replace(c, ast_must=[s.replace("const ", "")
                                       for s in c.ast_must])
    if not check_case(mutant_case):
        escaped.append("S4: dropping const from an AST expectation was not "
                       "detected")

    # S5: drop `volatile` from an expectation -> check_case must fail.
    c = _probe_case(cases, "bare/as4-post-volatile/core")
    if c is None:
        escaped.append("S5: probe case bare/as4-post-volatile/core missing")
    else:
        mutant_case = replace(c, ast_must=[s.replace("volatile ", "")
                                           for s in c.ast_must])
        if not check_case(mutant_case):
            escaped.append("S5: dropping volatile from an AST expectation was "
                           "not detected")

    # S6: replace every AS4 object row with a writable `char x;`
    # (Alice's counterexample) -> the checker must fail.
    mutant = []
    for c in cases:
        if c.axes and c.axes[1] == "bare" and c.axes[0].startswith("as4"):
            mutant.append(replace(c, source="char x;\n"))
        else:
            mutant.append(c)
    if not check_cases(mutant):
        escaped.append("S6: substituting a writable `char x;` for the AS4 "
                       "object rows was not detected")

    # S7: inject an unexpected discarded-qualifier warning into a positive
    # cell -> fail-closed diagnostic matching must fail.
    c = _probe_case(cases, "array/as0/core")
    inject = ("typedef char __code WarnChar;\n"
              "void warnfn(WarnChar *p) { char *r = p; (void)r; }\n")
    mutant_case = replace(c, source=inject + c.source)
    if not check_case(mutant_case):
        escaped.append("S7: an unexpected discarded-qualifier warning was not "
                       "detected")

    # S8: expect success for a negative cell -> must fail.
    c = _probe_case(cases, "member/as4/core")
    mutant_case = replace(c, errors=[], no_ir_reason="",
                          ast_must=["const char"],
                          ir_must=[r"@x = "])
    if not check_case(mutant_case):
        escaped.append("S8: expecting success for a negative cell was not "
                       "detected")

    # S9: remove a required probe -> coverage must fail.
    c = _probe_case(cases, "pointee/as4/core")
    mutant = [replace(x, probes=[]) if x.name == c.name else x for x in cases]
    if not validate_coverage(mutant):
        escaped.append("S9: removing a required mutation probe was not "
                       "detected")

    # S10: declare an AS0 cell with `__code` but keep AS0 expectations.
    c = _probe_case(cases, "bare/as0/core")
    mutant_case = replace(c, source="char __code x;\n")
    if not check_case(mutant_case):
        escaped.append("S10: `__code` on an AS0-control cell was not detected")

    # S11: shrink a *generation* axis in place -- drop `param-array` from
    # AXIS_FORM, exactly Alice's R6 counterexample.  The frozen literal tables
    # must still define the full acceptance product, so both the axis compare
    # and the product coverage must fail.
    global AXIS_FORM
    save_form = AXIS_FORM
    try:
        AXIS_FORM = [f for f in save_form if f != "param-array"]
        if len(product_cells()) != FROZEN_PRODUCT:
            escaped.append("S11: product_cells() followed the shrunk "
                           "generation axis instead of the frozen tables")
        mutant = build_product() + build_extras()
        if not validate_coverage(mutant):
            escaped.append("S11: shrinking the generation axis AXIS_FORM was "
                           "not detected against the frozen product")
    finally:
        AXIS_FORM = save_form

    # S12: blank a positive cell's `ast_decl` (Alice's R6 `ast_decl = None`
    # escape) -> the structural rule must fail the cell instead of silently
    # skipping the AST half while `ast_must` still claims coverage.
    c = _probe_case(cases, "bare/as4/core")
    mutant_case = replace(c, ast_decl=None)
    if not check_case(mutant_case):
        escaped.append("S12: a positive cell with ast_decl=None silently "
                       "skipped its AST check")

    # S12b: point `ast_decl` at a name that does not exist in the source ->
    # the AST half must fail (the "decl name matches nothing" escape).
    c = _probe_case(cases, "bare/as4/core")
    mutant_case = replace(c, ast_decl="no_such_entity")
    if not check_case(mutant_case):
        escaped.append("S12b: an ast_decl that matches no declaration was not "
                       "detected")

    # S12c: well-formed identifiers that only occur inside the type are not
    # declaration names.  They must not turn AST lookup into an empty shell.
    for fake_name in ("const", "char", "address_space"):
        if not check_case(replace(c, ast_decl=fake_name)):
            escaped.append(f"S12c: fake ast_decl={fake_name!r} matched type text")

    # S13: edit a frozen table together with its generation axis (a
    # "coordinated re-freeze" escape) -> the independently written
    # FROZEN_COUNTS cardinality must still fail the run.
    global FROZEN_FORM
    save_frozen = FROZEN_FORM
    save_form = AXIS_FORM
    try:
        FROZEN_FORM = tuple(f for f in save_frozen if f != "param-array")
        AXIS_FORM = [f for f in save_form if f != "param-array"]
        if not frozen_axis_failures():
            escaped.append("S13: a coordinated shrink of AXIS_FORM and "
                           "FROZEN_FORM escaped the frozen cardinality check")
    finally:
        FROZEN_FORM = save_frozen
        AXIS_FORM = save_form

    return escaped


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clang", default="clang")
    ap.add_argument("--self-test", action="store_true",
                    help="validate grid coverage and run the mutation "
                         "self-tests (deleting/dropping/injecting must fail)")
    opts = ap.parse_args()

    global CLANG
    CLANG = opts.clang

    product = build_product()
    extras = build_extras()
    cases = product + extras

    coverage = validate_coverage(cases)
    if coverage:
        sys.stderr.write("mcs251-as4-implicit-const-grid: COVERAGE FAIL\n")
        for f in coverage:
            sys.stderr.write("  " + f + "\n")
        return 1

    if opts.self_test:
        escaped = run_self_tests()
        if escaped:
            sys.stderr.write(
                "mcs251-as4-implicit-const-grid: SELF-TEST FAIL "
                f"({len(escaped)} mutations escaped)\n")
            for e in escaped:
                sys.stderr.write("  " + e + "\n")
            return 1
        print(f"mcs251-as4-implicit-const-grid: {len(product)} product cells "
              f"({len(AXIS_QUAL)}x{len(AXIS_FORM)}x{len(AXIS_SPELL)}) + "
              f"{len(extras)} extra-grid cases, self-tests PASS")
        return 0

    failures = check_cases(cases)
    if failures:
        sys.stderr.write(
            f"mcs251-as4-implicit-const-grid: FAIL ({len(failures)})\n")
        for f in failures:
            sys.stderr.write("  " + f.replace("\n", "\n  ") + "\n")
        return 1
    print(f"mcs251-as4-implicit-const-grid: {len(product)} product cells + "
          f"{len(extras)} extra-grid cases PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
