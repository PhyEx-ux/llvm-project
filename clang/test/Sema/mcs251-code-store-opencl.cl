// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL1.2 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL2.0 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -cl-std=CL3.0 -fsyntax-only -verify %s

// OpenCL registration is a language gate, not target unreachability (X1-11).
// Both spellings write argument 1, including typedef/attribute spellings.
typedef half __code CodeHalf;
void forbidden(CodeHalf *p, half __attribute__((address_space(4))) *q) {
  __builtin_store_half(1.0f, p); // expected-error {{cannot store to an MCS251 '__code' object}}
  __builtin_store_halff(1.0f, q); // expected-error {{cannot store to an MCS251 '__code' object}}
}
void allowed(half __xdata *p, half *q) {
  __builtin_store_half(1.0f, p);
  __builtin_store_halff(1.0f, q);
}
float read_code(CodeHalf *p) { return __builtin_load_halff(p); }
