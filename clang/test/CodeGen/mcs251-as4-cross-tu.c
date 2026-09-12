// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -I %S/Inputs -emit-llvm \
// RUN:   -disable-llvm-passes -o %t.caller.ll %s
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 \
// RUN:   -I %S/Inputs -emit-llvm \
// RUN:   -disable-llvm-passes -o %t.impltu.ll %S/Inputs/mcs251-as4-cross-tu-callee.c
// RUN: %python %S/Inputs/check-as4-cross-tu.py --caller %t.caller.ll \
// RUN:   --callee %t.impltu.ll --clang %clang \
// RUN:   --inputs-dir %S/Inputs --tmp-dir %t.gen
// RUN: %python %S/Inputs/check-as4-cross-tu.py --self-test \
// RUN:   --caller %t.caller.ll --callee %t.impltu.ll
// RUN: FileCheck %s --check-prefix=CALLER < %t.caller.ll
// RUN: FileCheck %s --check-prefix=IMPL < %t.impltu.ll

// A2c (RUNTIME-AS-PTR-DESIGN-A.md §2.7, §3-A2c): cross-TU compilation with
// the implicit-const CODE type.  This caller TU and the definition TU
// (Inputs/mcs251-as4-cross-tu-callee.c, sharing Inputs/mcs251-as4-cross-tu.h)
// are compiled SEPARATELY; check-as4-cross-tu.py then proves their
// ABI-visible signatures agree on address space, const and shape, and that
// the AS4 -> AS0 use sites materialise as addrspacecast.
//
// The link/run half of A2c under a supported ABI is exercised by
// validation/mcs251-xdata-e2e/a3-cross-tu-e2e.sh (clang -> llc ELF objects
// -> lld -> ihex -> QEMU, byte-exact), together with the firmware
// Inputs/mcs251-as4-cross-tu-fw.c.  lit cannot depend on the mcs251
// lld/QEMU installation, so the split is deliberate; this file pins
// separate compilation, matching signatures and real conversions.
//
// The design explicitly does NOT require an ordinary linker to diagnose
// invisible C type incompatibility: those diagnostics are checked where the
// compiler sees both declarations, in
// clang/test/Sema/mcs251-as4-cross-tu-merge.c (and the merge grid rows of
// mcs251-as4-implicit-const-grid.py).

#include "mcs251-as4-cross-tu.h"

// Module-level facts first (they must match before the first LABEL).
// The extern table keeps AS4 and const in the caller TU too.
// CALLER-DAG: @shared_rom = external addrspace(4) constant [8 x i8]

// A CODE-typed argument passed to a CODE-typed parameter keeps AS4 with no
// conversion at the call site.
uint8 pass_code(const uint8 __code *p, unsigned i) {
  return code_read(p, i);
}
// CALLER-LABEL: define dso_local zeroext i8 @pass_code(
// CALLER: call zeroext addrspace(4) i8 @code_read(ptr addrspace(4)

// The conversion happens exactly when a CODE source is used as a generic
// AS0 pointer; the IR records addrspacecast, never a bitcast or ptrtoint.
uint8 pass_generic(void) {
  return generic_read(shared_rom, 1);
}
// CALLER-LABEL: define dso_local zeroext i8 @pass_generic(
// CALLER: call zeroext addrspace(4) i8 @generic_read(ptr noundef addrspacecast (ptr addrspace(4) @shared_rom to ptr)

// A value returned across TUs is an AS4 pointer; converting it for generic
// use is again an addrspacecast on the caller side.
const uint8 *base_as_plain(void) {
  return code_base();
}
// CALLER-LABEL: define dso_local ptr @base_as_plain(
// CALLER: [[RET:%.*]] = call addrspace(4) ptr addrspace(4) @code_base()
// CALLER: [[P:%.*]] = addrspacecast ptr addrspace(4) [[RET]] to ptr
// CALLER: ret ptr [[P]]

// The extern table keeps AS4 and const in the caller TU too.
uint8 read_shared(unsigned i) {
  return shared_rom[i & 7u];
}
// CALLER-LABEL: define dso_local zeroext i8 @read_shared(
// CALLER: load i8, ptr addrspace(4)

// A CODE pointer returned across TUs is retargetable (the implicit const is
// on the pointee, not the pointer object), and dereference stays an AS4 load.
uint8 retarget_and_read(const uint8 __code *p, unsigned i) {
  const uint8 __code *q = p;
  return q[i & 7u];
}
// CALLER-LABEL: define dso_local zeroext i8 @retarget_and_read(
// CALLER: store ptr addrspace(4) %{{.*}}, ptr %q
// CALLER: load i8, ptr addrspace(4)

// An AS4 pointer produced by code_lookup crosses the TU boundary unchanged
// (the ABI returns it as an AS4 pointer, not a converted AS0 one).
uint8 lookup_and_read(unsigned i) {
  const uint8 __code *q = code_lookup(i);
  return *q;
}
// CALLER-LABEL: define dso_local zeroext i8 @lookup_and_read(
// CALLER: call addrspace(4) ptr addrspace(4) @code_lookup(i32
// CALLER: load i8, ptr addrspace(4)

// The callee module: all four entry points keep their frozen signatures and
// the table is an AS4 constant; no CODE object is ever stored into.
// IMPL-DAG: @shared_rom = addrspace(4) constant [8 x i8]
// IMPL: define dso_local ptr addrspace(4) @code_lookup(
// IMPL: define dso_local zeroext i8 @code_read(ptr addrspace(4)
// IMPL: define dso_local ptr addrspace(4) @code_base(
// IMPL: define dso_local zeroext i8 @generic_read(ptr noundef
// IMPL-NOT: store {{.*, ptr addrspace\(4\)}}
