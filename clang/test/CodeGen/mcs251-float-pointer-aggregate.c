// WP5 A3 parity, end to end through clang: a global struct that pairs a
// binary32 member with a placed-storage pointer member (`__xdata` = AS3,
// `__code` = AS4) is a registered v2 object. The float member is the
// 4-byte IEEE-754 bit pattern written through the same initializer channel
// as an i32, so the shared placement walk (hasV1PlacementInitializer) must
// admit it exactly like the storage whitelist already does (WP5 A3): the
// float image differs from an int one only in the first four payload bytes,
// the record grammar and relocation channel are identical.
//
// REQUIRES: mcs251-registered-target

// RUN: split-file %s %t
// RUN: %clang_cc1 -triple mcs251-unknown-none -std=c11 -ffreestanding -emit-llvm -o %t.ll %t/positive.c
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.o %t.ll
//
// The two float+pointer aggregates are emitted as sparse v1 XINIT records
// (u16 addr, u16 size, u16 payload-size, payload). Each placed object sits
// at its own section offset 0, so both records report DSEG address 0000.
// The payload is big-endian 1.5f = 3f c0 00 00 (then the 4-byte pointer
// container of @x) and 2.5f = 40 20 00 00 (then the container of @c).
// RUN: %python %S/Inputs/mcs251-float-xinit-check.py %t.o | FileCheck %s --check-prefix=XINIT
// XINIT: 0000 0008 0008 3fc0000000000000
// XINIT: 0000 0008 0008 4020000000000000
//
// The f64 aggregate must not become reachable: the module keeps the v2
// capability rejection (doubling the leaf width is not part of this slice).
// RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj -o /dev/null %t/f64.ll 2>&1 | FileCheck %s --check-prefix=F64
// RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj -o /dev/null %t/inttoptr.ll 2>&1 | FileCheck %s --check-prefix=INTPTR
// RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj -o /dev/null %t/undef.ll 2>&1 | FileCheck %s --check-prefix=UNDEF
//
// F64: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
// INTPTR: LLVM ERROR: MCS251 contract violation: global 'g': absolute-address (integer-to-pointer cast) pointer initialization is not supported
// UNDEF: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity

//--- positive.c
int __xdata x;
struct S { float f; int __xdata *p; };
struct S g = { 1.5f, &x };

char __code c;
struct T { float f; char __code *p; };
struct T h = { 2.5f, &c };
//--- f64.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

%D = type { double, ptr addrspace(3) }

@x = addrspace(3) global i32 0, align 1
@g = global %D { double 1.500000e+00, ptr addrspace(3) @x }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
//--- inttoptr.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

%struct.S = type { float, ptr addrspace(3) }

@g = global %struct.S { float 1.500000e+00, ptr addrspace(3) inttoptr (i32 16 to ptr addrspace(3)) }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
//--- undef.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

%struct.S = type { float, ptr addrspace(3) }

@x = addrspace(3) global i32 0, align 1
@g = global %struct.S { float undef, ptr addrspace(3) @x }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
