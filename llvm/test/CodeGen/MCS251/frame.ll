; RUN: llc -mtriple=mcs251 < %s | FileCheck %s

; Phase 9: static stack frames. The stack grows UP; the prologue allocates
; the frame with inc spx step instructions, the epilogue mirrors with dec,
; and frame objects are addressed as negative @dr60 displacements from the
; post-prologue stack top (the SDCC `@spx-0x....` style). (Loads of just-
; stored constants are forwarded by the DAG combiner, so the interesting
; visible pattern here is the store side plus the frame steps.)

define i8 @frame_single() {
; CHECK-LABEL: frame_single:
; CHECK: inc spx, #0x1
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: dec spx, #0x1
; CHECK: eret
  %b = alloca i8
  store i8 7, ptr %b
  %v = load i8, ptr %b
  ret i8 %v
}

; A four-byte local array written byte by byte: the frame is 4 bytes (one
; inc spx,#0x4) and the stores walk the displacements 0,-1,-2,-3 downwards
; from the frame top (the stores are emitted last-element first here).
define i8 @frame_array() {
; CHECK-LABEL: frame_array:
; CHECK: inc spx, #0x4
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: mov @dr60-0x0001, r{{[0-9]+}}
; CHECK: mov @dr60-0x0002, r{{[0-9]+}}
; CHECK: mov @dr60-0x0003, r{{[0-9]+}}
; CHECK: dec spx, #0x4
; CHECK: eret
  %a = alloca [4 x i8]
  %p0 = getelementptr inbounds i8, ptr %a, i16 0
  store i8 1, ptr %p0
  %p1 = getelementptr inbounds i8, ptr %a, i16 1
  store i8 2, ptr %p1
  %p2 = getelementptr inbounds i8, ptr %a, i16 2
  store i8 3, ptr %p2
  %p3 = getelementptr inbounds i8, ptr %a, i16 3
  store i8 4, ptr %p3
  %q0 = load i8, ptr %p0
  %q1 = load i8, ptr %p1
  %q2 = load i8, ptr %p2
  %q3 = load i8, ptr %p3
  %s01 = add i8 %q0, %q1
  %s23 = add i8 %q2, %q3
  %s = add i8 %s01, %s23
  ret i8 %s
}

; An i16 local: the frame-relative i16 store decomposes into two byte
; stores through the frame (ST16S pseudo), displacements dis and dis+1.
define i16 @frame_i16() {
; CHECK-LABEL: frame_i16:
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0001, r{{[0-9]+}}
; CHECK: mov @dr60, r{{[0-9]+}}
; CHECK: dec spx, #0x2
; CHECK: eret
  %w = alloca i16
  store i16 559, ptr %w
  %v = load i16, ptr %w
  ret i16 %v
}

; Frame adjustment steps: a 6-byte frame takes 4+2, mirrored by 2+4 on the
; way out. The single store's displacement is object offset 1 minus frame
; size 6 = -5.
define void @frame_steps() {
; CHECK-LABEL: frame_steps:
; CHECK: inc spx, #0x4
; CHECK: inc spx, #0x2
; CHECK: mov @dr60-0x0005, r{{[0-9]+}}
; CHECK: dec spx, #0x4
; CHECK: dec spx, #0x2
; CHECK: eret
  %a = alloca [6 x i8]
  %p = getelementptr inbounds i8, ptr %a, i16 0
  store i8 1, ptr %p
  ret void
}

; Taking the address of a frame object (pointer value escaping into the
; return): the pointer materialises from the two SFR-direct SP reads
; (0x81 = SP, 0x85 = SPH on this platform) plus a folded displacement.
define ptr @frame_addr() {
; CHECK-LABEL: frame_addr:
; CHECK: inc spx, #0x1
; CHECK: mov r{{[0-9]+}}, 0x81
; CHECK: mov r{{[0-9]+}}, 0x85
; CHECK: add wr{{[0-9]+}}, #
; CHECK: dec spx, #0x1
; CHECK: eret
  %b = alloca i8
  store i8 3, ptr %b
  ret ptr %b
}
