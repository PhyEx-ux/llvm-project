; G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4): byte-exact goldens for the complete
; TFPU load-trigger-wait-readback window, all nine connected commands, at both
; ends of the llc opt-level range.
;
; The window (design §2.4, "展开序列（RA 后）") is
;
;   mov DR4, <a>            ; AR window load  (design step 1)
;   mov DR0, <b>            ; BR window load, binary four only
;   mov 0xED, #<cmd>        ; trigger, the pinned 75 ED <cmd> bytes (step 2)
;   nop x <worst-case clk>  ; fixed delay chain (step 3)
;   mov <dst>, DR4          ; readback (step 4)
;
; and this test pins EVERY byte of it, in both forms the review demanded:
;
;   * the ELF .text bytes of each function, sliced by symbol
;     (Inputs/check-tfpu-golden.py);
;   * the assembly text, whose instruction sequence is the same shape
;     (FileCheck below, ASM0/ASM2 prefixes).
;
; Both forms run. The byte check runs from the object route, the instruction
; check from the assembly route (there is no MCS-251 assembler to re-assemble
; the text, so FileCheck pins the instruction stream directly).
;
; The comparison is exhaustive rather than substring-based: a wrong AR/BR
; window load register, a wrong command code, a wrong readback lane, a shifted
; trigger, or a surplus/missing wait byte all change the compared bytes. The
; checker's --self-test additionally mutates the REAL accepted bytes and
; requires the same comparison to reject each. The six injection classes are
; wrong-AR-load, wrong-BR-load, wrong-command, missing-wait-byte, wrong-
; readback-lane and changed-length; each injection is AIMED AT THE REAL FIELD,
; located by scanning the artifact (see check-tfpu-golden.py's "FIELD
; LOCATION" note), and the located byte is asserted against that field's
; architectural encoding before mutating it -- so the self-test cannot pass by
; mutating a neighbouring byte.
;
; Readback placement (the blocker this revision fixes): the readback is its own
; post-RA pseudo TFPU_RD_AR, expanded into `mov <park>, dr4` immediately after
; the wait chain. It must NOT be lowered to a COPY from dr4 -- the allocator
; coalesced the result vreg into dr4, the copy folded, and the first real read
; became the consumer in the epilogue, i.e. OUTSIDE the TPIN window. The
; assembly checks below therefore require `mov dr12, dr4` (O2) as the
; instruction right after the wait chain.
;
; RUN: %python %S/Inputs/check-tfpu-golden.py --help > /dev/null
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -filetype=obj -mcs251-object-format=elf %s -o %t.o2
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o %t.s0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o %t.s2
; RUN: FileCheck %s --check-prefix=ASM0 < %t.s0
; RUN: FileCheck %s --check-prefix=ASM2 < %t.s2
;
; Unary five: AR half only (the prologue ends at `mov dr4, dr12`).
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_sin  --shape o0 --cmd 2d --nops 270 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_sin  --shape o2 --cmd 2d --nops 270 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_cos  --shape o0 --cmd 2e --nops 270 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_cos  --shape o2 --cmd 2e --nops 270 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_tan  --shape o0 --cmd 2f --nops 258 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_tan  --shape o2 --cmd 2f --nops 258 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_atan --shape o0 --cmd 30 --nops 175 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_atan --shape o2 --cmd 30 --nops 175 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_sqrt --shape o0 --cmd 20 --nops 54 --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_sqrt --shape o2 --cmd 20 --nops 54 --self-test
;
; Binary four: the BR half (`mov dr0, dr12`) is the last window load and abuts
; the trigger; the AR half (`mov dr4, dr12`) precedes it. The two DR windows
; are physically disjoint (dr4 = r4-r7, dr0 = r0-r3).
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_add --shape o0 --cmd 1c --nops 40 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_add --shape o2 --cmd 1c --nops 40 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_sub --shape o0 --cmd 1d --nops 40 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_sub --shape o2 --cmd 1d --nops 40 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_mul --shape o0 --cmd 1e --nops 34 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_mul --shape o2 --cmd 1e --nops 34 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_div --shape o0 --cmd 1f --nops 67 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_div --shape o2 --cmd 1f --nops 67 --binary --self-test
;
; The same-operand shape: tfpu_mul(x, x). Both window loads read the SAME
; parking register (dr12 is the whole class), the AR load immediately before
; the BR one, and only one of the two reads is killing. This is the shape
; that caught the expand pass forcing kill on both loads.
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o0 --symbol _t_same --shape o0_same --cmd 1e --nops 34 --binary --self-test
; RUN: %python %S/Inputs/check-tfpu-golden.py --obj %t.o2 --symbol _t_same --shape o2_same --cmd 1e --nops 34 --binary --self-test
;
; Assembly text: the same sequence, so the byte golden and the instruction
; stream cannot diverge. There is no MCS-251 assembler in this build (llvm-mc
; reports "this target does not support assembly parsing"), so the text route
; is pinned with FileCheck rather than re-assembled. Every one of the nine
; commands is named at both opt levels, with its own worst-case wait count,
; AND the readback instruction that follows it inside the window.
;
; The shape of the window at O2: the parking register dr12 receives the
; parameter bytes, then the AR window load (`mov dr4, dr12`) immediately
; precedes the trigger, and `mov dr12, dr4` (the readback) immediately follows
; the wait chain. `mov dpl, r7`-style epilogue reads are NOT what the window
; reads: the snapshot is the dr12 move.
; ASM2-LABEL: _t_sin:
; ASM2: mov r12, a
; ASM2: mov r13, b
; ASM2: mov r14, dph
; ASM2: mov r15, dpl
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2d
; ASM2-COUNT-270: nop
; ASM2-NEXT: mov dr12, dr4
; ASM2-NEXT: mov dpl, r15
; ASM2-NEXT: mov dph, r14
; ASM2-NEXT: mov b, r13
; ASM2-NEXT: mov a, r12
; ASM2-NEXT: eret
;
; ASM2-LABEL: _t_cos:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2e
; ASM2-COUNT-270: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_tan:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2f
; ASM2-COUNT-258: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_atan:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x30
; ASM2-COUNT-175: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_sqrt:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x20
; ASM2-COUNT-54: nop
; ASM2-NEXT: mov dr12, dr4
;
; Binary: both window loads before the trigger, BR (dr0) last so it abuts it.
; ASM2-LABEL: _t_add:
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1c
; ASM2-COUNT-40: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_sub:
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1d
; ASM2-COUNT-40: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_mul:
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
; ASM2-NEXT: mov dr12, dr4
;
; ASM2-LABEL: _t_div:
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1f
; ASM2-COUNT-67: nop
; ASM2-NEXT: mov dr12, dr4
;
; Same-operand shape at O2: both loads from dr12, then trigger/wait/readback.
; ASM2-LABEL: _t_same:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
; ASM2-NEXT: mov dr12, dr4
;
; The O0 shapes: the same nine triggers and wait counts through the byte-wise
; window materialisation, and the same in-window readback (`mov dr12, dr4`).
; ASM0-LABEL: _t_sin:
; ASM0: mov dr4, dr12
; ASM0-NEXT: mov 0xed, #0x2d
; ASM0-COUNT-270: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_cos:
; ASM0: mov 0xed, #0x2e
; ASM0-COUNT-270: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_tan:
; ASM0: mov 0xed, #0x2f
; ASM0-COUNT-258: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_atan:
; ASM0: mov 0xed, #0x30
; ASM0-COUNT-175: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_sqrt:
; ASM0: mov 0xed, #0x20
; ASM0-COUNT-54: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_add:
; ASM0: mov dr0, dr12
; ASM0-NEXT: mov 0xed, #0x1c
; ASM0-COUNT-40: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_sub:
; ASM0: mov 0xed, #0x1d
; ASM0-COUNT-40: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_mul:
; ASM0: mov 0xed, #0x1e
; ASM0-COUNT-34: nop
; ASM0-NEXT: mov dr12, dr4
;
; ASM0-LABEL: _t_div:
; ASM0: mov 0xed, #0x1f
; ASM0-COUNT-67: nop
; ASM0-NEXT: mov dr12, dr4

target triple = "mcs251"

declare i32 @llvm.mcs251.tfpu.sin(i32)
declare i32 @llvm.mcs251.tfpu.cos(i32)
declare i32 @llvm.mcs251.tfpu.tan(i32)
declare i32 @llvm.mcs251.tfpu.atan(i32)
declare i32 @llvm.mcs251.tfpu.sqrt(i32)
declare i32 @llvm.mcs251.tfpu.add(i32, i32)
declare i32 @llvm.mcs251.tfpu.sub(i32, i32)
declare i32 @llvm.mcs251.tfpu.mul(i32, i32)
declare i32 @llvm.mcs251.tfpu.div(i32, i32)

define float @t_sin(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.sin(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_cos(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.cos(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_tan(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.tan(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_atan(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.atan(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_sqrt(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.sqrt(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_add(float %a, float %b) {
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.add(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_sub(float %a, float %b) {
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.sub(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_mul(float %a, float %b) {
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.mul(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_div(float %a, float %b) {
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.div(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

; Both operands the same value: the singleton parking class is read twice.
define float @t_same(float %x) {
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.mul(i32 %b, i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

!mcs251.signatures = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9}
!0 = !{!"_t_sin", i32 9, i32 0, i32 0}
!1 = !{!"_t_cos", i32 9, i32 0, i32 0}
!2 = !{!"_t_tan", i32 9, i32 0, i32 0}
!3 = !{!"_t_atan", i32 9, i32 0, i32 0}
!4 = !{!"_t_sqrt", i32 9, i32 0, i32 0}
!5 = !{!"_t_add", i32 9, i32 0, i32 0}
!6 = !{!"_t_sub", i32 9, i32 0, i32 0}
!7 = !{!"_t_mul", i32 9, i32 0, i32 0}
!8 = !{!"_t_div", i32 9, i32 0, i32 0}
!9 = !{!"_t_same", i32 9, i32 0, i32 0}
