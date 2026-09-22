// WP5 A3: binary32 global storage. A `float` global is a 4-byte leaf whose
// initializer is the IEEE-754 bit pattern written through the same
// initializer channel the integer scalars use, so the byte image is
// big-endian 1.5f = 3f c0 00 00. Every bit pattern -- signed zero, Inf, NaN
// payloads, subnormals -- is reinterpreted, never converted, and survives
// unchanged. The read-only (const/CODE) table uses the identical leaf
// treatment; true f64 stays rejected.
//
// REQUIRES: mcs251-registered-target

float p_one_five = 1.5f;
float p_minus_zero = -0.0f;
float p_inf = __builtin_inff();
float p_nan = __builtin_nanf("0x12345");
float p_min_sub = 0x1p-149f;
float p_max_fin = 0x1.fffffep+127f;
const float ro_two_five = 2.5f;

// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.o %t.ll
//
// The XINIT payload is the big-endian bit pattern of each float: the record is
// u16 addr, u16 size, u16 payload-size, payload. The companion checker reads
// the frozen v1 grammar directly and prints "<addr> <size> <paylen> <hex>".
// RUN: %python %S/Inputs/mcs251-float-xinit-check.py %t.o | FileCheck %s --check-prefix=XINIT
// XINIT: 0000 0004 0004 3fc00000
// XINIT: 0000 0004 0004 80000000
// XINIT: 0000 0004 0004 7f800000
// XINIT: 0000 0004 0004 7fc12345
// XINIT: 0000 0004 0004 00000001
// XINIT: 0000 0004 0004 7f7fffff
//
// The RO scalar goes to the read-only image (2.5f == 0x40200000), not the
// init table.
// RUN: %python %S/Inputs/mcs251-float-xinit-check.py --section .text %t.o | FileCheck %s --check-prefix=RO
// RO: 40200000
//
// True f64 must not become reachable through this change.
// RUN: printf 'target triple = "mcs251"\n@d = global double 1.500000e+00, align 1\n' > %t.double.ll
// RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t.double.ll 2>&1 | FileCheck %s --check-prefix=F64
// F64: LLVM ERROR: MCS251: defined global data requires byte-aligned default-address-space i8/i16/i32 scalar
//
// An i1 global remains outside the storage whitelist (A1 covers parameters
// only, not ordinary i1 globals).
// RUN: printf 'target triple = "mcs251"\n@g = global i1 true, align 1\n' > %t.bool.ll
// RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -filetype=obj -o /dev/null %t.bool.ll 2>&1 | FileCheck %s --check-prefix=I1
// I1: LLVM ERROR: MCS251: defined global data requires byte-aligned default-address-space i8/i16/i32 scalar
