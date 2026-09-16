// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR --implicit-check-not=llvm.used
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O1 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR --implicit-check-not=llvm.used
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR --implicit-check-not=llvm.used
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O3 -emit-llvm -o - %s | FileCheck %s --check-prefix=IR --implicit-check-not=llvm.used
// End to end through the REAL optimization pipeline and the G11-B emitter:
// clang -O2 IR -> llc object -> independent NOTE oracle. All three records
// (AS0 object, AS3 object, function; sizes 4/4/0) must survive to the
// .mcs251.placement table of the final object.
// RUN: %clang_cc1 -triple mcs251 -std=c11 -Werror -O2 -emit-llvm -o %t.ll %s
// RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t.ll -o %t.o
// RUN: %python %S/../../../llvm/test/CodeGen/MCS251/Inputs/check-placement-note.py %t.o \
// RUN:   ghost 0 0 1 0x200 4 1 0 \
// RUN:   ghostx 1 0 1 0x10000 4 1 0 \
// RUN:   ghostfn 2 1 1 0xFC4000 0 4 0
// G11-B R2 (review 2026-09-16 §二): the same TU carries THREE unreferenced
// bind declarations -- an AS0 object, an AS3 (__xdata) object and a
// function. The optimization pipeline deletes unreferenced external
// declarations AFTER CodeGenModule::Release() returns, taking the
// mcs251-place attributes with it, which is why Release() itself (after the
// final attribute refresh, before the optimizers run) appends every bind
// carrier to llvm.compiler.used -- the coordinator's pre-ruled keepalive
// root. Before that A-layer registration the -O2/-O3 modules below had NO
// ghost declarations left at all. Exactly three members (the initializer's
// type fixes the count; optimization levels may canonically reorder the
// printed member order, so the members are DAG-checked).
//
// IR: @ghost = external{{.*}} global i32
// IR: @ghostx = external{{.*}} addrspace(3) global i32
// IR-DAG: @llvm.compiler.used = appending global [3 x ptr]
// IR-DAG: ptr @ghost
// IR-DAG: ptr addrspacecast (ptr addrspace(3) @ghostx to ptr)
// IR-DAG: ptr addrspacecast (ptr addrspace(4) @ghostfn to ptr)
// IR: declare{{.*}} void @ghostfn(){{.*}}
// The three placement contracts survive every optimization level verbatim.
// IR: "mcs251-place"="0x200,data,object,bind,0" "mcs251-stable-symbol"="ghost"
// IR: "mcs251-place"="0x10000,xdata,object,bind,0" "mcs251-stable-symbol"="ghostx"
// IR: "mcs251-place"="0xFC4000,code,function,bind,0" "mcs251-stable-symbol"="ghostfn"

extern int ghost __attribute__((mcu_bind_at(0x200)));
extern __attribute__((address_space(3))) int ghostx __attribute__((mcu_bind_at(0x10000)));
extern void ghostfn(void) __attribute__((mcu_bind_at(0xFC4000)));

int keep(void) { return 1; }
