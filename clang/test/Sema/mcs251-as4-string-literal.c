// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil -fsyntax-only -verify %s

// A2a/A2b string-literal interaction (RUNTIME-AS-PTR-DESIGN-A.md §2.1/§3-A2,
// "字符串字面量交互"). The implicit-const rule must NOT change the existing
// string-literal semantics:
//
//   * a literal initializing a pointer INTO Code is a CODE-resident constant
//     object (X1-4, unchanged);
//   * a literal initializing an ordinary `const char *`/`char *` keeps the
//     ordinary C rules and the ordinary address space (AS0);
//   * explicit const, implicit const and the plain C rules must compose
//     without new spurious diagnostics on the legal forms.

// CODE-targeted literal: the anonymous object is AS4-resident.
char __code *code_str = "code literal";   // no diagnostic

// Ordinary literals are untouched.
const char *plain_const = "plain";
char *plain_mut = "mutable";

// Composing explicit const with the CODE target is legal.
const char __code *explicit_code_const = "explicit";

// A CODE pointer *object* is const (the pointer itself is an object in AS4),
// so retargeting it is rejected -- but the initializer is fine.
char * __code frozen_ptr = "frozen"; // expected-note {{variable 'frozen_ptr' declared const here}}
void retarget(void) {
  frozen_ptr = "other"; // expected-error {{cannot assign to variable 'frozen_ptr' with const-qualified type}}
}

// A literal passed to a `char * __code` parameter (a *value* parameter whose
// pointee is AS4) converts: the literal already denotes a CODE object.
void takes_code(char __code *p);
void call_code_literal(void) { takes_code("arg"); }

// A literal passed to a plain char* parameter is not converted to CODE:
// the literal stays AS0 (ordinary C), so a de-const cast is required.
void takes_plain(char *p);
void call_plain_literal(void) {
  takes_plain((char *)"arg");
}

// Two literals keep distinct addresses/objects (the per-occurrence node is
// unique); comparing them is the ordinary C pointer comparison, with the
// ordinary -Wstring-compare warning.
int distinct(void) { return "one" == "two"; } // expected-warning {{result of comparison against a string literal is unspecified}}
