// WP5 A1: `_Bool` parameter ABI (updated for stack-based reentrant ABI).
// A C `_Bool` parameter reaches the backend as `i1`, which legalizes onto the
// i8 register channel. The first parameter arrives in DPL; continuation `_Bool`
// arguments arrive in a 1-byte stack slot below the return address (no static
// `_PARM_` / OSEG overlay slots). The return path stays functional.
//
// REQUIRES: mcs251-registered-target

extern unsigned char sink;

// The first `_Bool` parameter is carried in DPL (the i8 first-argument
// location).
void f(_Bool b) { sink = (unsigned char)b; }

// A continuation `_Bool` is passed as a 1-byte stack argument (no OSEG slot).
void g(unsigned char a, _Bool b) { sink = (unsigned char)(a + b); }

// The return path was already functional and must stay so.
_Bool r(void) { return 1; }

// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.ir.o %t.ll
// RUN: llvm-nm %t.ir.o | FileCheck %s --check-prefix=NMIR
// NMIR-DAG: T _f
// NMIR-DAG: T _g
// NMIR-DAG: T _r
// NMIR-NOT: _PARM_

//--- Object level: no OSEG parameter slots ---------------------------------
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -mllvm -mcs251-object-format=elf -emit-obj -o %t.o %s
// RUN: llvm-readelf -s -S %t.o | FileCheck %s --check-prefix=OBJ
// OBJ-DAG: FUNC{{.*}}GLOBAL{{.*}}_f
// OBJ-DAG: FUNC{{.*}}GLOBAL{{.*}}_g
// OBJ-DAG: FUNC{{.*}}GLOBAL{{.*}}_r
// OBJ-NOT: _g_PARM_2
// OBJ-NOT: .mcs251.OSEG

//--- Assembly level: continuation `_Bool` loaded from stack frame ----------
// RUN: llc -mtriple=mcs251-unknown-none -O1 -filetype=asm -o - %t.ll | FileCheck %s --check-prefix=ASM
// ASM-LABEL: _g:
// ASM: mov r{{[0-9]+}}, dpl
// ASM: mov r{{[0-9]+}}, @dr60-0x{{[0-9a-f]+}}
// ASM-NOT: _g_PARM_2
