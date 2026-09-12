// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -x objective-c -fsyntax-only -verify %s

// X1-10: the third write-effect authority is the CodeGen custom emitter. The
// z/OS va_list spellings carry plain NoThrow signatures and have no
// SemaChecking custom checker, so the first two sweeps (type strings X1-7,
// SemaChecking X1-8) could not see their writes; they were callable with a
// __code va_list and the write only surfaced as an AS4 store in the emitted
// IR (Alice X1-5 evidence: zos_end.ll stores ptr null into the first slot).
// __builtin_zos_va_end stores a null pointer into slot 0 of the va_list named
// by argument 0 and __builtin_zos_va_copy memcpies into argument 0, so both
// are modeled writes with the destination in argument 0.
// __builtin_objc_memmove_collectable registers for all languages and routes
// argument 0 to the GC write-barrier memmove; it was audited in the same
// CGBuiltin.cpp sweep (43 write-effect groups, all covered by table rows).

// AS4 (CODE) va_list destinations are diagnosed at the call site.
void zos_end_code(char * __code *p) {
  __builtin_zos_va_end(p); // expected-error {{cannot store to an MCS251 '__code' object}} // expected-warning {{discards qualifiers}}
}
void zos_copy_code(char * __code *dst, char **src) {
  __builtin_zos_va_copy(dst, src); // expected-error {{cannot store to an MCS251 '__code' object}} // expected-warning {{discards qualifiers}}
}
void objc_collectable_code(char * __code *dst, char **src) {
  __builtin_objc_memmove_collectable(dst, src, 8); // expected-error {{cannot store to an MCS251 '__code' object}} // expected-warning {{discards qualifiers}}
}
void objc_collectable_both_code(char __code *dst, const char __code *src) {
  __builtin_objc_memmove_collectable(dst, src, 8); // expected-error {{cannot store to an MCS251 '__code' object}} // expected-warning {{discards qualifiers}}
}
void objc_collectable_read_code(char __xdata *dst, const char __code *src) {
  __builtin_objc_memmove_collectable(dst, src, 8);
}

// AS3 (XDATA) and generic va_lists are ordinary uses: the CODE check does not
// fire (checked with -fsyntax-only; full lowering of zos_va_copy is a
// separate backend matter outside this file's scope).
void zos_end_xdata(char * __xdata *p) { __builtin_zos_va_end(p); }
void zos_copy_xdata(char * __xdata *dst, char **src) {
  __builtin_zos_va_copy(dst, src);
}
void zos_end_generic(char **p) { __builtin_zos_va_end(p); }
void zos_copy_generic(char **dst, char **src) { __builtin_zos_va_copy(dst, src); }
void objc_collectable_generic(char **dst, char **src) {
  __builtin_objc_memmove_collectable(dst, src, 8);
}

// The CODE va_list as the read-side operand stays legal: zos_va_copy only
// writes argument 0.
void zos_copy_from_code(char **dst, char * __code *src) {
  __builtin_zos_va_copy(dst, src); // expected-warning {{discards qualifiers}}
}

// The z/OS lifecycle for a generic va_list (array of two char*, decaying to
// char**): start/copy/end all pass.
void zos_lifecycle(int n, ...) {
  char *ls[2], *ms[2];
  __builtin_zos_va_start(ls, n);
  __builtin_zos_va_copy(ms, ls);
  __builtin_zos_va_end(ms);
  __builtin_zos_va_end(ls);
}
