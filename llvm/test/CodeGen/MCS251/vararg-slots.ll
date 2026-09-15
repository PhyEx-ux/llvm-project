; G2 B-S2 static-slot variadic ABI golden (G2-VARIADIC-DESIGN-draft.md R3
; §4.3): a variadic definition emits the six 4-byte continuation slots
; `_PARM_(F+1).._PARM_(F+6)` after its fixed slots, va_start anchors the
; 8-byte {base, offset} va_list pair on the FIRST continuation slot
; (F=1 -> _PARM_2, the printf shape), and a caller writes each variadic
; actual into one 4-byte slot in source order before the ecall.  The IR
; below is exactly what clang's MCS251ABIInfo::EmitVAArg produces for
; `int sum(int n, ...) { va_list ap; va_start(ap,n); return va_arg(ap,int); }`
; -- there is no llvm.va_arg anywhere: the guard/halt/load chain is
; already plain IR and the halt arm is the llvm.mcs251.vararg.halt
; intrinsic, which lowers to the two-byte dead loop `sjmp .` (80 FE).
;
; Review-fix coverage (Alice blockers 1-2): every consumer type of the
; frozen slot table is pinned here -- i32, f32 (bit pattern in the same
; 4-byte slot), ordinary pointer (canonicalized A byte on both the caller
; store and the consumer load), and the narrow i8/i16 reads (the slot is
; always the promoted 4-byte one; only the relevant low bytes are read and
; the offset still advances exactly 4).  The caller boundaries 0 / 1 / 6
; variadic actuals are each asserted, a NON-LEAF variadic definition
; (sum_nonleaf calls twice) pins its slot area in non-overlayable DSEG
; while the leaf ones stay in OSEG, and the +int16 pair-width guard lives
; in clang/test/CodeGen/mcs251-vararg-ir.c.

; RUN: llc -mtriple=mcs251 < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj < %s | llvm-readelf -sW - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj < %s | llvm-readelf -x .text - | FileCheck %s --check-prefix=HALT

%struct.__va_list_tag = type { ptr, i32 }

@gv = global i32 77

define i32 @sum(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load i32, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %v
}

define i32 @callit() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 1, i32 30, i32 40)
  ret i32 %r
}

; f32 consumption: the slot's 4 bytes are the IEEE-754 bit pattern
; (double == f32 on this target); read and advance identical to i32.
define float @consume_f32(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load float, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret float %v
}

; Pointer consumption: one 4-byte slot, canonicalized A byte.
define ptr @consume_ptr(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load ptr, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret ptr %v
}

; Narrow i8 consumption: the slot always holds the promoted i32; the read
; is load-i32 + trunc, and the offset still advances exactly one slot.
define i8 @consume_i8(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %w = load i32, ptr %addr, align 1
  %v = trunc i32 %w to i8
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i8 %v
}

; Narrow i16 consumption: same 4-byte slot, low half read.
define i16 @consume_i16(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %w = load i32, ptr %addr, align 1
  %v = trunc i32 %w to i16
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i16 %v
}

define internal i32 @twice(i32 %x) addrspace(4) {
  %r = mul i32 %x, 2
  ret i32 %r
}

; NON-LEAF variadic definition: sum_nonleaf calls twice, so its slot area
; must survive the nested call -- DSEG, not the overlayable OSEG.
define i32 @sum_nonleaf(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load i32, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  %r = call i32 @twice(i32 %v)
  ret i32 %r
}

; Boundary 0: no variadic actual -- no slot store at all before the ecall.
define i32 @call_zero() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5)
  ret i32 %r
}

; Boundary 1: exactly one variadic actual (_PARM_2 only).
define i32 @call_one() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5, i32 7)
  ret i32 %r
}

; Boundary 6: the cap -- all six continuation slots written, no seventh.
define i32 @call_six() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6)
  ret i32 %r
}

; f32 actual: 1.5f lands in the 4-byte slot as big-endian 3F C0 00 00.
define float @call_f32() addrspace(4) {
  %r = call float (i32, ...) @consume_f32(i32 2, float 1.500000e+00)
  ret float %r
}

; Pointer actual: the global's 32-bit address with the A byte zeroed.
define ptr @call_ptr() addrspace(4) {
  %r = call ptr (i32, ...) @consume_ptr(i32 2, ptr @gv)
  ret ptr %r
}

; Narrow actuals ride the same 4-byte slots after promotion: 0x5A as
; 00 00 00 5A and 0x1122 as 00 00 11 22.
define i8 @call_narrow() addrspace(4) {
  %r = call i8 (i32, ...) @consume_i8(i32 3, i32 90, i32 4386)
  ret i8 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare void @llvm.mcs251.vararg.halt()

!mcs251.signatures = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12}
!0 = !{!"_sum", i32 9, i32 0, i32 0}
!1 = !{!"_callit", i32 1, i32 0, i32 0}
!2 = !{!"_consume_f32", i32 9, i32 0, i32 0}
!3 = !{!"_consume_ptr", i32 9, i32 0, i32 0}
!4 = !{!"_consume_i8", i32 9, i32 0, i32 0}
!5 = !{!"_consume_i16", i32 9, i32 0, i32 0}
!6 = !{!"_sum_nonleaf", i32 9, i32 0, i32 0}
!7 = !{!"_call_zero", i32 1, i32 0, i32 0}
!8 = !{!"_call_one", i32 1, i32 0, i32 0}
!9 = !{!"_call_six", i32 1, i32 0, i32 0}
!10 = !{!"_call_narrow", i32 1, i32 0, i32 0}
!11 = !{!"_call_f32", i32 1, i32 0, i32 0}
!12 = !{!"_call_ptr", i32 1, i32 0, i32 0}

; ---------------------------------------------------------------------------
; Slot areas: leaf variadic definitions overlay (OSEG), the non-leaf one
; (contains a call) does not (DSEG); every continuation slot is 4 bytes.
; ---------------------------------------------------------------------------
; ASM:      .area OSEG (OVR,DATA)
; ASM-NEXT:  .globl _sum_PARM_2
; ASM:      _sum_PARM_2:
; ASM-NEXT:  .ds 4
; ASM:      _sum_PARM_3:
; ASM-NEXT:  .ds 4
; ASM:      _sum_PARM_4:
; ASM-NEXT:  .ds 4
; ASM:      _sum_PARM_5:
; ASM-NEXT:  .ds 4
; ASM:      _sum_PARM_6:
; ASM-NEXT:  .ds 4
; ASM:      _sum_PARM_7:
; ASM-NEXT:  .ds 4

; ---------------------------------------------------------------------------
; sum: va_start anchors the pair on THIS function's first continuation
; slot (_sum_PARM_2, F=1), guard, halt arm, one-slot 4-byte read+advance.
; ---------------------------------------------------------------------------
; ASM:      _sum:
; ASM:       .globl _sum_PARM_2
; ASM:       .db 0x7e, 0x08, (_sum_PARM_2) >> 8, (_sum_PARM_2)
; ASM:       .db 0x7a, 0x0c, 0x00, (_sum_PARM_2) >> 16
; ASM:       mov @dr60-0x0007, r0
; ASM:       mov @dr60-0x0004, r3
; ASM:       mov r8, #0x00
; ASM:       mov @dr0, r8
; ASM:       mov @dr0+0x0003, r8

; Guard: `icmp ule i32 %off, 20` canonicalises to an unsigned compare
; against 21 (0x15); out-of-range flows to the halt arm.
; ASM:       mov dr12, #0x0015
; ASM:       cmp dr0, dr12

; Halt arm: the frozen two-byte dead loop.
; ASM:      ; %halt
; ASM-NEXT:  sjmp .

; Load arm: base+off addressing, one 4-byte big-endian slot read, then the
; offset advances by exactly one slot.
; ASM:      ; %load
; ASM:       mov r12, @dr60-0x0007
; ASM:       mov r15, @dr60-0x0004
; ASM:       add dr12, dr0
; ASM:       mov r4, @dr12
; ASM:       mov r7, @dr12+0x0003
; ASM:       mov @dr60-0x0003, r0
; ASM:       mov @dr60, r3

; Caller: variadic actuals 30 and 40 land in _PARM_2/_PARM_3 as 4-byte
; big-endian values, the fixed argument n=1 goes through the DPL register
; channel, and the slot stores all precede the ecall.
; ASM:      _callit:
; ASM:       .db 0x7e, 0x18, (_sum_PARM_2) >> 8, (_sum_PARM_2)
; ASM:       .db 0x7a, 0x1c, 0x00, (_sum_PARM_2) >> 16
; ASM:       mov r1, #0x1e
; ASM:       mov @dr4+0x0003, r1
; ASM:       .globl _sum_PARM_3
; ASM:       .db 0x7e, 0x18, (_sum_PARM_3) >> 8, (_sum_PARM_3)
; ASM:       .db 0x7a, 0x1c, 0x00, (_sum_PARM_3) >> 16
; ASM:       mov r0, #0x28
; ASM:       mov @dr4+0x0003, r0
; ASM:       mov dpl, r1
; ASM:       ecall _sum

; ---------------------------------------------------------------------------
; f32 consumption (consume_f32): the load arm reads the same 4 slot bytes
; and returns them as the raw bit pattern -- identical addressing and
; advance to the i32 form.
; ---------------------------------------------------------------------------
; Leaf type consumer: its own slot area is overlayable OSEG like sum's.
; ASM:      .area OSEG (OVR,DATA)
; ASM-NEXT:  .globl _consume_f32_PARM_2
; ASM:      _consume_f32:
; ASM:       .globl _consume_f32_PARM_2
; ASM:      ; %load
; ASM:       mov r4, @dr12
; ASM:       mov r7, @dr12+0x0003

; ---------------------------------------------------------------------------
; Pointer consumption (consume_ptr): value bytes 1..3 of the canonical
; 4-byte slot; the A byte is re-normalized to zero on the read side.
; ---------------------------------------------------------------------------
; ASM:      .area OSEG (OVR,DATA)
; ASM-NEXT:  .globl _consume_ptr_PARM_2
; ASM:      _consume_ptr:
; ASM:       .globl _consume_ptr_PARM_2
; ASM:      ; %load
; ASM:       mov r4, @dr12+0x0001
; ASM:       mov r6, @dr12+0x0003
; ASM:       mov r0, #0x00

; ---------------------------------------------------------------------------
; Narrow i8 consumption (consume_i8): load-i32 + trunc reduces to reading
; the slot's LOW byte (@dr12+0x0003, big-endian); the advance is still the
; 4-byte `add dr0, dr4` and the offset store stays a full register pair.
; ---------------------------------------------------------------------------
; ASM:      .area OSEG (OVR,DATA)
; ASM-NEXT:  .globl _consume_i8_PARM_2
; ASM:      _consume_i8:
; ASM:       .globl _consume_i8_PARM_2
; ASM:      ; %load
; Slot address = base + offset, then the post-consumption advance by the
; full constant 4 (dr4 was set to #0x0004 above), not the narrowed width.
; ASM:       add dr12, dr0
; ASM-NEXT:  add dr0, dr4
; ASM-NEXT:  mov r4, @dr12+0x0003
; The updated offset (r0-r3) is written back to the va_list field in full.
; ASM-NEXT:  mov @dr60-0x0003, r0
; ASM-NEXT:  mov @dr60-0x0002, r1
; ASM-NEXT:  mov @dr60-0x0001, r2
; ASM-NEXT:  mov @dr60, r3
; ASM-NEXT:  mov dpl, r4

; ---------------------------------------------------------------------------
; Narrow i16 consumption (consume_i16): the slot's low HALF (bytes 2..3).
; ---------------------------------------------------------------------------
; ASM:      .area OSEG (OVR,DATA)
; ASM-NEXT:  .globl _consume_i16_PARM_2
; ASM:      _consume_i16:
; ASM:       .globl _consume_i16_PARM_2
;
; va_start zeroes the offset field (and sets up the constant-4 stride
; register) before any consumption happens.
; ASM:       mov dr4, #0x0004
; ASM-NEXT:  add dr0, dr4
; ASM:       mov r8, #0x00
; ASM-NEXT:  mov @dr0, r8
; ASM-NEXT:  mov @dr0+0x0001, r8
; ASM-NEXT:  mov @dr0+0x0002, r8
; ASM-NEXT:  mov @dr0+0x0003, r8
; ASM:      ; %load
; Slot address, then the post-consumption full-slot advance (constant 4).
; ASM:       add dr12, dr0
; ASM-NEXT:  add dr0, dr4
; ASM-NEXT:  mov r4, @dr12+0x0002
; ASM-NEXT:  mov r5, @dr12+0x0003
; The updated offset (r0-r3) is written back to the va_list field in full.
; ASM-NEXT:  mov @dr60-0x0003, r0
; ASM-NEXT:  mov @dr60-0x0002, r1
; ASM-NEXT:  mov @dr60-0x0001, r2
; ASM-NEXT:  mov @dr60, r3

; ---------------------------------------------------------------------------
; NON-LEAF form: sum_nonleaf makes a call, so its six slots are in
; non-overlayable DSEG (the assertion the review asked for) and the body
; keeps the same va_start anchor and 4-byte read, then the nested call.
; ASM:      .area DSEG (DATA)
; ASM-NEXT:  .globl _sum_nonleaf_PARM_2
; ASM:      _sum_nonleaf_PARM_2:
; ASM-NEXT:  .ds 4
; ASM:      _sum_nonleaf_PARM_7:
; ASM-NEXT:  .ds 4
; ASM:      _sum_nonleaf:
; ASM:       .globl _sum_nonleaf_PARM_2
; ASM:       ecall _twice

; ---------------------------------------------------------------------------
; Boundary 0 (call_zero): zero variadic actuals -- no slot symbol is even
; referenced between function entry and the ecall; only the fixed argument
; rides the register channel.
; ---------------------------------------------------------------------------
; ASM:      _call_zero:
; ASM-NOT:  _sum_PARM_
; ASM:      ecall _sum

; ---------------------------------------------------------------------------
; Boundary 1 (call_one): exactly one slot store (0x07 into _PARM_2), no
; _PARM_3 reference.
; ---------------------------------------------------------------------------
; ASM:      _call_one:
; ASM:       .db 0x7e, 0x18, (_sum_PARM_2) >> 8, (_sum_PARM_2)
; ASM-NOT:  _sum_PARM_3
; ASM:       mov r0, #0x07
; ASM-NEXT:  mov @dr4+0x0003, r0
; ASM-NOT:  _sum_PARM_
; ASM:      ecall _sum

; ---------------------------------------------------------------------------
; Boundary 6 (call_six): all six continuation slots written in source
; order (1..6), nothing after _PARM_7 before the ecall.
; ---------------------------------------------------------------------------
; ASM:      _call_six:
; ASM:       .globl _sum_PARM_7
; ASM:       mov r0, #0x06
; ASM-NEXT:  mov @dr4+0x0003, r0
; ASM-NOT:  _sum_PARM_
; ASM:      ecall _sum

; ---------------------------------------------------------------------------
; f32 actual (call_f32): 1.5f stored as big-endian 3F C0 00 00 in the
; callee's first continuation slot.
; ---------------------------------------------------------------------------
; ASM:      _call_f32:
; ASM:       mov r0, #0x3f
; ASM-NEXT:  .db 0x7e, 0x18, (_consume_f32_PARM_2) >> 8, (_consume_f32_PARM_2)
; ASM-NEXT:  .db 0x7a, 0x1c, 0x00, (_consume_f32_PARM_2) >> 16
; ASM-NEXT:  mov @dr4, r0
; ASM-NEXT:  mov r0, #0xc0
; ASM-NEXT:  mov @dr4+0x0001, r0
; ASM-NEXT:  mov r0, #0x00
; ASM-NEXT:  mov @dr4+0x0002, r0
; ASM-NEXT:  mov @dr4+0x0003, r0

; ---------------------------------------------------------------------------
; Pointer actual (call_ptr): MOVADDR32 materialises @gv's 32-bit address,
; the A byte is zeroed on the store side too.
; ---------------------------------------------------------------------------
; ASM:      _call_ptr:
; ASM:       .db 0x7e, 0x08, (_gv) >> 8, (_gv)
; ASM:       .db 0x7a, 0x0c, 0x00, (_gv) >> 16
; ASM:       mov r8, #0x00
; ASM:       .db 0x7e, 0x18, (_consume_ptr_PARM_2) >> 8, (_consume_ptr_PARM_2)
; ASM:       .db 0x7a, 0x1c, 0x00, (_consume_ptr_PARM_2) >> 16
; ASM:       mov @dr4, r8
; ASM:       mov @dr4+0x0003, r3

; ---------------------------------------------------------------------------
; Narrow actuals (call_narrow): promoted values in the same 4-byte slots
; -- 0x5A as 00 00 00 5A, 0x1122 as 00 00 11 22.
; ---------------------------------------------------------------------------
; ASM:      _call_narrow:
; ASM:       mov r1, #0x5a
; ASM-NEXT:  mov @dr4+0x0003, r1
; ASM:       .globl _consume_i8_PARM_3
; ASM:       mov r0, #0x11
; ASM-NEXT:  mov @dr4+0x0002, r0
; ASM-NEXT:  mov r0, #0x22
; ASM-NEXT:  mov @dr4+0x0003, r0

; Object: the six continuation slots are 4-byte global STT_OBJECT symbols
; in the definition's slot area section -- leaf (sum, the type consumers)
; and non-leaf (sum_nonleaf) alike.
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_3
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_4
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_5
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_6
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_PARM_7
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_f32_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_f32_PARM_7
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_ptr_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_ptr_PARM_7
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_i8_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_i8_PARM_7
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_i16_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume_i16_PARM_7
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_nonleaf_PARM_2
; OBJ: {{[0-9]+}}: {{[0-9a-f]+}}     4 OBJECT GLOBAL DEFAULT {{[0-9]+}} _sum_nonleaf_PARM_7

; The halt encoding is the frozen byte pair 80 FE (sjmp to itself); every
; halt arm in this file (one per consumer) uses it.
; HALT: 80fe
