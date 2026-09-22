// WP5 A1: `_Bool` parameter ABI. A C `_Bool` parameter reaches the backend as
// `i1`, which legalizes onto the i8 register channel. It is admitted as a
// deliberately narrow second non-identical ArgVT/VT pair (VT = i8,
// ArgVT = i1) next to the pre-existing softened-f32 pair, and its continuation
// arguments use the ordinary 1-byte `_<callee>_PARM_n` static slot. The
// rejection path for every other sub-byte width, and the return path, are
// unchanged.
//
// Part 1 (object from IR): every `_Bool` parameter shape lowers, the first
// parameter needs no `_f_PARM_2`, and a continuation `_Bool` publishes
// `_g_PARM_2` as a 1-byte OSEG object. The return path stays functional.
//
// REQUIRES: mcs251-registered-target

extern unsigned char sink;

// The first `_Bool` parameter is carried in DPL (the i8 first-argument
// location); it needs no continuation slot.
void f(_Bool b) { sink = (unsigned char)b; }

// A continuation `_Bool` uses the named 1-byte static slot `_g_PARM_2`, the
// same channel an `unsigned char` continuation parameter uses.
void g(unsigned char a, _Bool b) { sink = (unsigned char)(a + b); }

// The return path was already functional and must stay so.
_Bool r(void) { return 1; }

// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251-unknown-none -mcs251-object-format=elf -filetype=obj -o %t.ir.o %t.ll
// RUN: llvm-nm %t.ir.o | FileCheck %s --check-prefix=NMIR
// NMIR-DAG: T _f
// NMIR-DAG: B _g_PARM_2
// NMIR-DAG: T _g
// NMIR-DAG: T _r
// NMIR-NOT: _f_PARM_2

//--- Object level: continuation slot shape ---------------------------------
// RUN: %clang_cc1 -triple mcs251-unknown-none -ffreestanding -mllvm -mcs251-object-format=elf -emit-obj -o %t.o %s
// RUN: llvm-readelf -s -S %t.o | FileCheck %s --check-prefix=OBJ
// OBJ-DAG: FUNC{{.*}}GLOBAL{{.*}}_f
// OBJ-DAG: FUNC{{.*}}GLOBAL{{.*}}_r
// `_g_PARM_2` is a 1-byte object in the per-function OSEG overlay area.
// OBJ-DAG: 1 OBJECT  GLOBAL DEFAULT {{[0-9]+}} _g_PARM_2
// OBJ-DAG: .mcs251.OSEG.{{[0-9]+}} {{.*}}000001
