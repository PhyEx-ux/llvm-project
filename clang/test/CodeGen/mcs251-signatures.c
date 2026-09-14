// P-4 (freeze 2026-09-14): the `!mcs251.signatures` named metadata that
// clang publishes for the object-level function signatures.
//
// clang is the ONLY producer: the AST still sees `bit` vs `unsigned char`,
// and the freeze forbids re-deriving a signature from the i8 boundary.  These
// checks pin the source-typed export, the completeness rule (every external
// declaration, used or not) and the re-declaration merge rule (the record
// must describe the most complete declaration, never the canonical/earliest
// one).
//
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -emit-llvm -o - %s | FileCheck %s --implicit-check-not=static_local
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not=static_local

// Every external declaration is recorded, even one this TU never uses.  Node
// layout: !{!"<final-ELF-symbol>", i32 <role>, i32 <ret-bit>, i32 <param0>..}.
// role bit0 = definition, bit1 = declaration, bit2 = no-prototype, bit3 =
// variadic; the bitmap carries one entry per SOURCE parameter.

// An unused prototyped declaration with a bit parameter and a bit return.
// CHECK-DAG: !{!"_unused_bit", i32 2, i32 1, i32 1, i32 0}
typedef __bit BOOL;
extern BOOL unused_bit(BOOL a, int b);

// An unused plain declaration (all non-bit).
// CHECK-DAG: !{!"_unused_plain", i32 2, i32 0, i32 0, i32 0}
extern int unused_plain(int a, int b);

// A definition with a bit parameter.
// CHECK-DAG: !{!"_defined", i32 1, i32 0, i32 1, i32 0}
int defined(BOOL a, int b) { return a + b; }

// A K&R no-prototype declaration: role bit2 set, param_count 0 (the decoder
// enforces that a no-prototype record carries no source parameters).
// CHECK-DAG: !{!"_knr_decl", i32 6, i32 0}
void knr_decl();

// Re-declaration merge: `int merged();` then `int merged(int);` must record
// the PROTOTYPED, one-parameter shape, not the earliest zero-parameter one.
// CHECK-DAG: !{!"_merged", i32 2, i32 0, i32 0}
int merged();
int merged(int x);

// A definition after an old-style declaration: the definition wins, so this
// is a definition record with the real parameter list.
// CHECK-DAG: !{!"_late_def", i32 1, i32 0, i32 0}
int late_def();
int late_def(int x) { return x; }

// An asm label is the final ELF symbol and must not be re-prefixed.
// CHECK-DAG: !{!"asm_sig_name", i32 2, i32 0, i32 0}
int labelled(int x) __asm__("asm_sig_name");

// A local (static) function is outside the signature domain.
static int static_local(int x) { return x; }

// A variadic declaration: bit3 set, and only the FIXED parameters are
// counted (the `...` is not a source parameter).
// CHECK-DAG: !{!"_vararg", i32 10, i32 0, i32 0}
int vararg(int a, ...);

// A declaration nested INSIDE a function body still owns its source type and
// must be recorded with it: a top-level-only walk would miss it.
// CHECK-DAG: !{!"_nested_bit", i32 2, i32 1, i32 1}
int nested_caller(void) {
  extern BOOL nested_bit(BOOL);
  return nested_bit(1);
}

// A C11 `inline` definition with external linkage emits no object symbol when
// unused, so its role is a DECLARATION here, not a definition (the reader has
// no symbol to associate a definition with).  Same for a `static` function
// used only through inlining -- but a static one is out of the domain anyway.
// CHECK-DAG: !{!"_inline_unused", i32 2, i32 0, i32 0}
inline int inline_unused(int x) { return x + 1; }

// Keep at least one definition so the module is well-formed.
int main(void) { return defined(1, 2); }
