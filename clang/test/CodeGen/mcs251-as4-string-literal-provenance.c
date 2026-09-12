// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -fmcs251-keil \
// RUN:   -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

// A2a/A3 string-literal provenance IR contract
// (RUNTIME-AS-PTR-DESIGN-A.md §3-A2 "字符串字面量交互" and §3-A3 R5-②).
//
// The review finding: `strlen("literal in code")` is NOT a CODE-context
// source.  The literal decays to an AS0 pointer and no addrspacecast is
// involved, so a test that cites it as a "CODE string source" is mislabelled.
//
// This test pins the two provenances explicitly:
//   * a literal required by a `const char __code *` context is an
//     `addrspace(4) constant` global, and using it as a generic argument is a
//     real AS4 -> AS0 addrspacecast;
//   * a literal passed straight to a generic parameter stays a plain AS0
//     `constant` global with NO cast;
//   * both keep their NUL terminator (the linked-image byte checker binds the
//     same payloads).
//
// (The existing Sema test mcs251-as4-string-literal.c covers diagnostics;
// this file covers the IR/placement half.)

// Module-level facts (checked before the first function label): the
// CODE-context literal is an AS4 constant with its NUL, the plain one is a
// plain AS0 constant with its NUL, and they are distinct objects -- a
// compiler that merged them would erase the provenance distinction.
// CHECK-DAG: @.str = private unnamed_addr addrspace(4) constant [21 x i8] c"code context literal\00"
// CHECK-DAG: @.str.1 = private unnamed_addr constant [18 x i8] c"plain as0 literal\00"

typedef unsigned int size_t;
size_t strlen(const char *s);

void takes_code(const char __code *p);
void takes_plain(const char *p);

// A literal in a CODE context (parameter pointee is AS4): the anonymous array
// is addrspace(4) constant, and the call passes AS4 with no conversion needed.
void call_code_ctx(void) {
  takes_code("code context literal");
}
// CHECK-LABEL: define dso_local void @call_code_ctx(
// CHECK-NOT: addrspacecast
// CHECK: call addrspace(4) void @takes_code(ptr addrspace(4) noundef @.str)

// The same literal shape sent to a generic parameter stays AS0 and is passed
// with no conversion at all.
void call_plain(void) {
  takes_plain("plain as0 literal");
}
// CHECK-LABEL: define dso_local void @call_plain(
// CHECK-NOT: addrspacecast
// CHECK: call addrspace(4) void @takes_plain(ptr noundef @.str.1)

// A CODE-context literal used as a strlen argument: this is the ONLY form
// that legitimately exercises "CODE source into a generic primitive", and the
// conversion is explicit in the IR.
size_t len_code_ctx_via_ptr(void) {
  const char __code *p = "code context literal";
  return strlen(p);
}
// CHECK-LABEL: define dso_local i32 @len_code_ctx_via_ptr(
// CHECK: [[Q:%.*]] = load ptr addrspace(4), ptr
// CHECK: [[C:%.*]] = addrspacecast ptr addrspace(4) [[Q]] to ptr
// CHECK: call addrspace(4) i32 @strlen(ptr noundef [[C]])

// The plain literal to strlen: no conversion (the reviewed mislabelled case).
size_t len_plain(void) {
  return strlen("plain as0 literal");
}
// CHECK-LABEL: define dso_local i32 @len_plain(
// CHECK-NOT: addrspacecast
// CHECK: call addrspace(4) i32 @strlen(ptr noundef @.str.1)
