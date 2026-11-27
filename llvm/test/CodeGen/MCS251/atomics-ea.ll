; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llvm-readobj -h %t.o | FileCheck %s --check-prefix=ELF

; Verify default object format is ELF32BE (EM_MCS251 = 0x9999) without
; needing -mcs251-object-format=elf.
; ELF: Format: elf32-mcs251
; ELF: Arch: mcs251

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

; Single-core fence lowers to zero-byte compiler barrier (no runtime call).
; CHECK-LABEL: _test_fence:
; CHECK-NOT:   ecall
; CHECK:       eret
; O0-LABEL: _test_fence:
; O0-NOT:   ecall
; O0:       eret
define void @test_fence() addrspace(4) {
  fence seq_cst
  fence syncscope("singlethread") acq_rel
  ret void
}

; 1-byte atomic load/store: single hardware byte op (no EA toggle needed).
; CHECK-LABEL: _atomic_load_i8:
; CHECK-NOT:   jbc
; CHECK:       eret
define i8 @atomic_load_i8(ptr %p) addrspace(4) {
  %v = load atomic i8, ptr %p seq_cst, align 1
  ret i8 %v
}

; 4-byte unaligned (align 1) atomic load: bracketed by EA save-disable/restore.
; CHECK-LABEL: _atomic_load_i32_align1:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
; O0-LABEL: _atomic_load_i32_align1:
; O0:       jbc 0xaf,
; O0:       setb 0xaf
define i32 @atomic_load_i32_align1(ptr %p) addrspace(4) {
  %v = load atomic i32, ptr %p seq_cst, align 1
  ret i32 %v
}

; 2-byte atomic store: bracketed by EA save-disable/restore.
; CHECK-LABEL: _atomic_store_i16:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define void @atomic_store_i16(ptr %p, i16 %v) addrspace(4) {
  store atomic i16 %v, ptr %p release, align 1
  ret void
}

; 4-byte atomic store in __xdata (AS3): keeps AS3 MOVX channel inside EA critical section.
; CHECK-LABEL: _atomic_store_xdata_i32:
; CHECK:       jbc 0xaf,
; CHECK:       movx
; CHECK:       setb 0xaf
; CHECK:       eret
define void @atomic_store_xdata_i32(ptr addrspace(3) %p, i32 %v) addrspace(4) {
  store atomic i32 %v, ptr addrspace(3) %p seq_cst, align 1
  ret void
}

; Atomic RMW add/sub/and/or/xor/nand/xchg/min/max on i8/i16/i32.
; CHECK-LABEL: _atomic_rmw_add_i32:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define i32 @atomic_rmw_add_i32(ptr %p, i32 %v) addrspace(4) {
  %old = atomicrmw add ptr %p, i32 %v seq_cst, align 1
  ret i32 %old
}

; CHECK-LABEL: _atomic_rmw_xchg_i8:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define i8 @atomic_rmw_xchg_i8(ptr %p, i8 %v) addrspace(4) {
  %old = atomicrmw xchg ptr %p, i8 %v acq_rel, align 1
  ret i8 %old
}

; CHECK-LABEL: _atomic_rmw_nand_i16:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define i16 @atomic_rmw_nand_i16(ptr %p, i16 %v) addrspace(4) {
  %old = atomicrmw nand ptr %p, i16 %v seq_cst, align 1
  ret i16 %old
}

; CHECK-LABEL: _atomic_rmw_umax_i16:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define i16 @atomic_rmw_umax_i16(ptr %p, i16 %v) addrspace(4) {
  %old = atomicrmw umax ptr %p, i16 %v seq_cst, align 1
  ret i16 %old
}

; Compare-exchange on i32 (align 1).
; CHECK-LABEL: _atomic_cas_i32:
; CHECK:       jbc 0xaf,
; CHECK:       setb 0xaf
; CHECK:       eret
define i1 @atomic_cas_i32(ptr %p, i32 %cmp, i32 %new) addrspace(4) {
  %res = cmpxchg ptr %p, i32 %cmp, i32 %new seq_cst monotonic, align 1
  %ok = extractvalue { i32, i1 } %res, 1
  ret i1 %ok
}

; >4-byte atomic load/store/exchange/cas lower to generic __atomic_* libcalls.
; CHECK-LABEL: _atomic_load_8b_into:
; CHECK:       ecall ___atomic_load
; CHECK:       eret
define void @atomic_load_8b_into(ptr %src, ptr %dst) addrspace(4) {
  %v = load atomic i64, ptr %src seq_cst, align 1
  store i64 %v, ptr %dst, align 1
  ret void
}

; CHECK-LABEL: _atomic_store_8b_from:
; CHECK:       ecall ___atomic_store
; CHECK:       eret
define void @atomic_store_8b_from(ptr %dst, ptr %src) addrspace(4) {
  %v = load i64, ptr %src, align 1
  store atomic i64 %v, ptr %dst seq_cst, align 1
  ret void
}

!mcs251.signatures = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13}
!0 = !{!"_test_fence", i32 1, i32 0}
!1 = !{!"_atomic_load_i8", i32 1, i32 0, i32 0}
!2 = !{!"_atomic_load_i32_align1", i32 1, i32 0, i32 0}
!3 = !{!"_atomic_store_i16", i32 1, i32 0, i32 0, i32 0}
!4 = !{!"_atomic_store_xdata_i32", i32 1, i32 0, i32 0, i32 0}
!5 = !{!"_atomic_rmw_add_i32", i32 1, i32 0, i32 0, i32 0}
!6 = !{!"_atomic_rmw_xchg_i8", i32 1, i32 0, i32 0, i32 0}
!7 = !{!"_atomic_rmw_nand_i16", i32 1, i32 0, i32 0, i32 0}
!8 = !{!"_atomic_rmw_umax_i16", i32 1, i32 0, i32 0, i32 0}
!9 = !{!"_atomic_cas_i32", i32 1, i32 0, i32 0, i32 0, i32 0}
!10 = !{!"_atomic_load_8b_into", i32 1, i32 0, i32 0, i32 0}
!11 = !{!"_atomic_store_8b_from", i32 1, i32 0, i32 0, i32 0}
!12 = !{!"___atomic_load", i32 2, i32 0, i32 0, i32 0, i32 0, i32 0}
!13 = !{!"___atomic_store", i32 2, i32 0, i32 0, i32 0, i32 0, i32 0}
