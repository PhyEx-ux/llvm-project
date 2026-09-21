#!/usr/bin/env python3
"""Pin A2 at syntax time; retain every case's raw output when --output is used.

The cpp group must pass with round 8A and fail with round 7. The c group
must pass after round 9 and fail with round 8A. Each case is a separate TU,
so a rejected neighbor cannot hide an unexpected acceptance.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

CPP = {
    "variable-pattern": (0, "template<class T> int *p = (int*)0x1234;"),
    "class-pattern": (0, "template<class T> struct H { inline static int *p = (int*)0x1234; };"),
    "function-pattern": (0, "template<class T> int *f() { static int *p = (int*)0x1234; return p; }"),
    "legal-specialization": (0, "template<class T> int *p = (int*)0x1234; template<> int *p<int> = nullptr; int **q = &p<int>;"),
    "class-use": (1, "template<class T> struct H { inline static int *p = (int*)0x1234; }; int **q = &H<double>::p;"),
    "explicit-instantiation": (1, "template<class T> struct H { inline static int *p = (int*)0x1234; }; template struct H<double>;"),
    "function-instantiation": (1, "template<class T> int *f() { static int *p = (int*)0x1234; return p; } int *g() { return f<int>(); }"),
    "variable-instantiation": (1, "template<class T> int *p = (int*)0x1234; int **q = &p<int>;"),
    "absolute-specialization": (1, "template<class T> int *p = nullptr; template<> int *p<int> = (int*)0x1234;"),
    "conditional-selected": (1, "struct X {int *p;}; X x = true ? X{(int*)0x1234} : X{nullptr};"),
    "conditional-unselected": (0, "struct X {int *p;}; X x = true ? X{nullptr} : X{(int*)0x1234};"),
    "conditional-false-selected": (1, "struct X {int *p;}; X x = false ? X{nullptr} : X{(int*)0x1234};"),
    "conditional-false-unselected": (0, "struct X {int *p;}; X x = false ? X{(int*)0x1234} : X{nullptr};"),
    "conditional-parenthesized-selected": (1, "struct X {int *p;}; X x = false ? X{nullptr} : (X{(int*)0x1234});"),
    "conditional-parenthesized-unselected": (0, "struct X {int *p;}; X x = true ? X{nullptr} : (X{(int*)0x1234});"),
    "conditional-nested-selected": (1, "struct X {int *p;}; X x = true ? (false ? X{nullptr} : X{(int*)0x1234}) : X{nullptr};"),
    "conditional-nested-unselected": (0, "struct X {int *p;}; X x = false ? X{(int*)0x1234} : (true ? X{nullptr} : X{(int*)0x1234});"),
    "conditional-pattern": (0, "struct X {int *p;}; template<class T> X x = true ? X{(int*)0x1234} : X{nullptr};"),
    "conditional-instantiated": (1, "struct X {int *p;}; template<class T> X x = true ? X{(int*)0x1234} : X{nullptr}; X *q = &x<int>;"),
    "conditional-instantiated-null": (0, "struct X {int *p;}; template<class T> X x = true ? X{nullptr} : X{(int*)0x1234}; X *q = &x<int>;"),
    "constructor-absolute": (1, "struct X { int *p; constexpr X(int *q): p(q) {} }; X x{(int*)0x1234};"),
    "constructor-null": (0, "struct X { int *p; constexpr X(int *q): p(q) {} }; X x{nullptr};"),
    "constructor-symbol": (0, "struct X { int *p; constexpr X(int *q): p(q) {} }; int v; X x{&v};"),
    "sizeof-pointer": (0, "unsigned n = sizeof(true ? (int*)0 : (int*)0x1234);"),
    "sizeof-aggregate": (0, "struct X {int *p;}; unsigned n = sizeof(true ? X{(int*)0x1234} : X{nullptr});"),
}

C = {
    "c01": (0, "struct X {int *p;}; unsigned n = sizeof((struct X){(int*)0x1234});"),
    "c02": (0, "unsigned n = sizeof((int*){(int*)0x1234});"),
    "c03": (0, "struct X {int *p;}; void f(void) { unsigned n = sizeof((struct X){(int*)0x1234}); (void)n; }"),
    "c04": (1, "struct X {int *p;}; struct X x = (struct X){(int*)0x1234};"),
    "c05": (0, "struct X {int *p;}; struct X x = 1 ? (struct X){0} : (struct X){(int*)0x1234};"),
    "c06": (1, "struct X {int *p;}; struct X x = 1 ? (struct X){(int*)0x1234} : (struct X){0};"),
    "c07": (0, "struct X {int *p;}; struct X x = _Generic(0, int: (struct X){0}, default: (struct X){(int*)0x1234});"),
    "c08": (1, "struct X {int *p;}; struct X x = _Generic(0, int: (struct X){(int*)0x1234}, default: (struct X){0});"),
    "c09": (0, "struct X {int *p;}; unsigned n = _Alignof(__typeof__((struct X){(int*)0x1234}));"),
    "c10": (0, "struct X {int *p;}; struct X x = __builtin_choose_expr(1, (struct X){0}, (struct X){(int*)0x1234});"),
    "c11": (1, "int *p = (int*){(int*)0x1234};"),
    "c12": (0, "struct X {int *p;}; struct X x = (struct X){0};"),
    "c13": (0, "struct X {int *p;}; int v; struct X x = (struct X){&v};"),
    "c14": (0, "int *p = 1 ? (int*){0} : (int*){(int*)0x1234};"),
    "c15": (1, "struct X {int *p;}; struct Y {struct X x;}; struct Y y = (struct Y){(struct X){(int*)0x1234}};"),
    "c16": (1, "struct X {int *p;}; struct X *p = &(struct X){(int*)0x1234};"),
    "c17": (1, "struct X {int *p;}; int *p = ((struct X){(int*)0x1234}).p;"),
    # c18 is rejected by the ordinary C type constraint, not A2.
    "c18": (1, "struct X {int *p;}; int *p = (int*)(struct X){(int*)0x1234};"),
    "c19": (1, "struct X {int *p;}; void f(void) { static struct X x = (struct X){(int*)0x1234}; (void)x; }"),
    # c20 has automatic storage duration, unlike c19.
    "c20": (0, "struct X {int *p;}; void f(void) { struct X x = (struct X){(int*)0x1234}; (void)x; }"),
    "c21": (0, "struct X {int *p;}; unsigned n = sizeof((struct X){(int*)0x1234}.p);"),
    "c22": (0, "struct X {int *p;}; struct X x = 0 ? (struct X){(int*)0x1234} : (1 ? (struct X){0} : (struct X){(int*)0x1234});"),
    "typeof": (0, "struct X {int *p;}; __typeof__((struct X){(int*)0x1234}) x = {0};"),
    "types-compatible": (0, "struct X {int *p;}; int n = __builtin_types_compatible_p(__typeof__((struct X){(int*)0x1234}), struct X);"),
    "choose-selected": (1, "struct X {int *p;}; struct X x = __builtin_choose_expr(0, (struct X){0}, (struct X){(int*)0x1234});"),
    "generic-control": (0, "struct X {int *p;}; int n = _Generic((struct X){(int*)0x1234}, struct X: 1, default: 0);"),
    "address-unselected": (0, "struct X {int *p;}; struct X *p = 1 ? (struct X*)0 : &(struct X){(int*)0x1234};"),
    "address-symbol": (0, "struct X {int *p;}; int v; struct X *p = &(struct X){&v};"),
    "nested-address": (1, "struct X {int *p;}; struct X **p = &(struct X*){&(struct X){(int*)0x1234}};"),
    "integer-result": (1, "struct X {int *p;}; int n = (&(struct X){(int*)0x1234} != 0);"),
    "short-circuit": (0, "struct X {int *p;}; int n = 0 && (&(struct X){(int*)0x1234} != 0);"),
    "language-constant": (1, "int f(void); unsigned n = sizeof((int){f()});"),
}


def run(group, compiler, root):
    rows = []
    cases = CPP if group == "cpp" else C
    for name, (expected, source) in cases.items():
        src = root / (name + (".cpp" if group == "cpp" else ".c"))
        src.write_text(source + "\n")
        for opt in ("-O0", "-O2"):
            label = name + opt
            cmd = compiler + ["-triple", "mcs251", "-x", "c++" if group == "cpp" else "c", "-std=c++17" if group == "cpp" else "-std=c11", opt, "-fsyntax-only", str(src)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            (root / (label + ".stdout")).write_text(result.stdout)
            (root / (label + ".stderr")).write_text(result.stderr)
            (root / (label + ".rc")).write_text(str(result.returncode) + "\n")
            count = result.stderr.count("error: absolute-address")
            wanted = expected if name not in ("c18", "language-constant") else 0
            ok = result.returncode == expected and count == wanted
            if name == "c18":
                ok = ok and "where arithmetic or pointer type is required" in result.stderr
            if name == "language-constant":
                ok = ok and "not a compile-time constant" in result.stderr
            ok = ok and not any(s in result.stderr for s in ("PLEASE submit", "Stack dump", "LLVM ERROR"))
            rows.append(dict(case=name, opt=opt, rc=result.returncode, absolute=count, expected=expected, expected_absolute=wanted, passed=ok, command=cmd))
            print(f"{label}: {'PASS' if ok else 'FAIL'} rc={result.returncode}/{expected} absolute={count}/{wanted}")
            if not ok:
                print(result.stderr)
    (root / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    return int(any(not row["passed"] for row in rows))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", choices=("cpp", "c"), required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("compiler", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    compiler = args.compiler
    if compiler and compiler[0] == "--":
        compiler = compiler[1:]
    if not compiler:
        parser.error("compiler command required")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        raise SystemExit(run(args.group, compiler, args.output))
    with tempfile.TemporaryDirectory() as directory:
        raise SystemExit(run(args.group, compiler, Path(directory)))
