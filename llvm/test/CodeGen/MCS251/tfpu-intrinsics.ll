; G7 S3 (G7-FLOAT-DESIGN-draft.md §2.3 plan A / §2.4): the TFPU DMA
; coprocessor family, all nine connected commands (PM D2/D3).
;
; The IR-level contract: the intrinsic signature is the f32 IEEE-754 BIT
; PATTERN as i32 (the SIGNATURE NOTE in IntrinsicsMCS251.td -- this target
; softens every f32 and the generic float type legalizer has no softening
; case for intrinsic nodes). The lowering is the §2.4 four-step window:
;
;   load  dr4 (AR; + dr0 for the binary four)   -- TFPU_LD_AR / TFPU_LD_BR
;   mov   0xED,#<cmd>        ; TFPU_TRG, bytes 75 ED <cmd>
;   nop   x  <worst-case>    ; fixed delay chain (manual 35.3 worst clocks)
;   read  dr4                ; TFPU_RD_AR -> mov <dst>, dr4
;
; Window ownership during register allocation is ONE unit per command (the two
; window-load pseudos, the trigger/wait pseudo and the readback pseudo):
;
;   TFPU_LD_AR $ar        ; implicit-def $dr4     AR half r4-r7
;   [TFPU_LD_BR $br]      ; implicit-def $dr0     BR half r0-r3 (binary)
;   TFPU_<OP>             ; implicit-def r0..r7, implicit-use $dr4[, $dr0]
;   %rd = TFPU_RD_AR      ; implicit-use $dr4     readback, def in gpr32rd
;
; The operand vregs are EXPLICIT and live in the singleton parking class
; gpr32win = {dr12}, which contains none of r0-r7. That is the blocker-1 fix,
; operand half: with the old CopyToReg form the allocator coalesced the
; operand vreg into dr4, the copy folded away, and the byte gather stayed
; pinned at its definition site -- so the AR window load could sit BEFORE the
; TPIN=0 write (measured at -O0 and -O2). Now the only writes into r0-r7 are
; the two window loads, which the post-RA MCS251TFPUExpand pass emits as the
; first steps of the window.
;
; The readback is the same fix, result half: as a `getCopyFromReg(DR4)` the
; allocator coalesced the result vreg into dr4 and the COPY folded away, so
; the first real read of the dr4 lanes became the consumer -- for a
; set -> sin -> clear -> return shape, `mov dpl, r7` AFTER `clr 0x90`. The
; TFPU_RD_AR pseudo defs an explicit vreg in gpr32rd = {dr0, dr12} (which
; excludes dr4 by construction) and expands to `mov <dst>, dr4` immediately
; after the wait chain, so the snapshot happens inside the window. dr12 is
; preferred; dr0 is the fallback when dr12 still holds a live operand (unary
; commands never use r0-r3, and a binary command's BR half is dead once the
; trigger has fired).
;
; Inputs/check-tfpu-window.py asserts the whole effect model on the MIR
; below: the command pseudo's implicit Defs are EXACTLY r0-r7 and nothing
; else, its implicit Uses are exactly dr4 (unary) / dr4+dr0 (binary), it has
; no explicit operand (vreg OR physical register), it is preceded by exactly
; its own ADJACENT IN-BLOCK window loads (AR then BR) with nothing in between,
; the loads' operands are parked in gpr32win, and the TFPU_RD_AR readback with
; a non-dr4 destination follows immediately. The checker's --self-test mutates
; the real MIR (dropped window def, out-of-window def, explicit physical
; operand, operand retyped into the window, readback retyped into the window,
; deleted readback, an unrelated instruction inserted inside the window, a
; block boundary inside the window, dropped BR use, deleted window load) and
; requires each mutation to be rejected.
;
; Command codes / worst-case clocks:
;   add 0x1C/40  sub 0x1D/40  mul 0x1E/34  div 0x1F/67  sqrt 0x20/54
;   sin 0x2D/270 cos 0x2E/270 tan 0x2F/258 atan 0x30/175
;
; QEMU stc32g144k246 behaviour for a DMAIR write is unproven (G1-0 open
; item): these are STATIC encoding goldens only, never execution asserts.
; The byte-level window golden lives in tfpu-golden.ll.

; Instruction selection at both ends of the llc opt-level range: the pseudo
; itself (with the complete window effect model) at finalize-isel, checked by
; the whole-window clobber assertion.
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -stop-after=finalize-isel %s -o %t.mir0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -stop-after=finalize-isel %s -o %t.mir2
; RUN: %python %S/Inputs/check-tfpu-window.py %t.mir0 --self-test
; RUN: %python %S/Inputs/check-tfpu-window.py %t.mir2 --self-test
;
; The same MIR in instruction form (the shapes that matter for the window
; ordering): the AR window load names a vreg explicitly and the command
; pseudo follows it.
; RUN: FileCheck %s --check-prefix=MIR0 < %t.mir0
; RUN: FileCheck %s --check-prefix=MIR2 < %t.mir2
;
; The expanded sequences (full pipeline, -verify-machineinstrs included).
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o - | FileCheck %s --check-prefix=ASM0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s --check-prefix=ASM2

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

; ---------------------------------------------------------------------------
; Unary five: only the AR window (dr4) is loaded; dr0 is NOT a window write.
; ---------------------------------------------------------------------------

define float @t_sin(float %x) {
; MIR0-LABEL: name: t_sin
; MIR0: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR0-NEXT: TFPU_SIN implicit-def dead $r0, implicit-def dead $r1, implicit-def dead $r2, implicit-def dead $r3, implicit-def $r4, implicit-def $r5, implicit-def $r6, implicit-def $r7, implicit $dr4
; MIR0-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; The unary window never writes the BR half (the next function label bounds
; the negative check).
; MIR0-NOT: TFPU_LD_BR
; MIR0-LABEL: name: t_cos
;
; The O2 shape: the softened f32 argument bytes are gathered into the parking
; register dr12 first, then the AR window load writes them into r4-r7
; (r4 = MSB ... r7 = LSB per the manual's figures and the RegisterInfo.td
; byte-order ruling), one trigger, the fixed 270-clock chain, then the
; readback in the reverse lane order.
; ASM2-LABEL: _t_sin:
; ASM2: mov r12, a
; ASM2: mov r13, b
; ASM2: mov r14, dph
; ASM2: mov r15, dpl
; The AR window load: dr12 -> dr4 (r4-r7).
; ASM2-NEXT: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2d
; ASM2-COUNT-270: nop
; The readback: the DR4 window is snapshotted into the parking register
; INSIDE the window (TFPU_RD_AR -> `mov dr12, dr4`), before the epilogue
; touches anything else. Reading the lanes at the return sites instead --
; i.e. the folded `COPY $dr4` this test used to require -- was the blocker:
; the read then happened after the TPIN=1 write that closes the window.
; ASM2-NEXT: mov dr12, dr4
; ASM2-NEXT: mov dpl, r15
; ASM2-NEXT: mov dph, r14
; ASM2-NEXT: mov b, r13
; ASM2-NEXT: mov a, r12
; ASM2-NEXT: eret
;
; The O0 shape is the same sequence: same AR window load, same trigger, same
; 270-clock chain and the same in-window readback (only the byte-wise
; materialisation around it differs).
; ASM0-LABEL: _t_sin:
; ASM0: mov dr4, dr12
; ASM0-NEXT: mov 0xed, #0x2d
; ASM0-COUNT-270: nop
; ASM0-NEXT: mov dr12, dr4
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.sin(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_cos(float %x) {
; MIR2-LABEL: name: t_cos
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_COS implicit-def
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_cos:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2e
; ASM2-COUNT-270: nop
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.cos(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_tan(float %x) {
; MIR2-LABEL: name: t_tan
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_TAN implicit-def
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_tan:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2f
; ASM2-COUNT-258: nop
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.tan(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_atan(float %x) {
; MIR2-LABEL: name: t_atan
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_ATAN implicit-def
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_atan:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x30
; ASM2-COUNT-175: nop
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.atan(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_sqrt(float %x) {
; MIR2-LABEL: name: t_sqrt
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_SQRT implicit-def
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_sqrt:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x20
; ASM2-COUNT-54: nop
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.sqrt(i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

; ---------------------------------------------------------------------------
; Binary four: AR into dr4, BR into dr0 -- the two windows are physically
; disjoint (r4-r7 vs r0-r3), both loaded before the trigger. The loads run
; BR-then-AR in the emitted code (both read the same parking register dr12
; at different times; the AR operand is dead once its window load has run).
; ---------------------------------------------------------------------------

define float @t_add(float %a, float %b) {
; MIR2-LABEL: name: t_add
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR killed %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_ADD implicit-def dead $r0, implicit-def dead $r1, implicit-def dead $r2, implicit-def dead $r3, implicit-def $r4, implicit-def $r5, implicit-def $r6, implicit-def $r7, implicit $dr4, implicit $dr0
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_add:
; ASM2: mov r12, a
; ASM2: mov r13, b
; ASM2: mov r14, dph
; ASM2: mov r15, dpl
; The second operand arrives through its _PARM_ static slot, gathered into
; the parking register and written into the BR window r0-r3 (never into the
; The AR window load into dr4 comes first, then (possibly after a dr12
; reload from the operand's spill slot -- a non-window register) the BR
; window load, then the trigger.
; ASM2-NOT: mov 0xed
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2: mov 0xed, #0x1c
; ASM2-COUNT-40: nop
; ASM2-NEXT: mov dr12, dr4
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.add(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_sub(float %a, float %b) {
; MIR2-LABEL: name: t_sub
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR killed %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_SUB implicit-def
; ASM2-LABEL: _t_sub:
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2: mov 0xed, #0x1d
; ASM2-COUNT-40: nop
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.sub(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_mul(float %a, float %b) {
; MIR2-LABEL: name: t_mul
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR killed %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_MUL implicit-def
; ASM2-LABEL: _t_mul:
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.mul(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

define float @t_div(float %a, float %b) {
; MIR2-LABEL: name: t_div
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR killed %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_DIV implicit-def
; ASM2-LABEL: _t_div:
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2: mov 0xed, #0x1f
; ASM2-COUNT-67: nop
  %ba = bitcast float %a to i32
  %bb = bitcast float %b to i32
  %r = call i32 @llvm.mcs251.tfpu.div(i32 %ba, i32 %bb)
  %f = bitcast i32 %r to float
  ret float %f
}

; ---------------------------------------------------------------------------
; Nested hardware ops: tfpu_mul(tfpu_sin(x), y). Two triggers, in source
; order. The sin result passes through the parking register (the allocator
; copies it out of dr4 and back in), so the multiply's operand setup never
; writes r0-r7 outside its own window loads.
; ---------------------------------------------------------------------------
define float @t_nested(float %x, float %y) {
; MIR2-LABEL: name: t_nested
; MIR2: TFPU_SIN implicit-def
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR killed %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_MUL implicit-def
; ASM2-LABEL: _t_nested:
; ASM2: mov 0xed, #0x2d
; ASM2-COUNT-270: nop
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
; ASM2-NEXT: mov dr12, dr4
  %bx = bitcast float %x to i32
  %by = bitcast float %y to i32
  %s = call i32 @llvm.mcs251.tfpu.sin(i32 %bx)
  %m = call i32 @llvm.mcs251.tfpu.mul(i32 %s, i32 %by)
  %f = bitcast i32 %m to float
  ret float %f
}

; ---------------------------------------------------------------------------
; Both operands the SAME value: tfpu_mul(x, x). The parking class is a
; singleton and the value is read twice, so at finalize-isel neither load
; kills its operand; the allocator then hands both loads the same dr12 and
; marks only ONE of the two reads killing. The expand pass must carry that
; kill flag through unchanged instead of forcing kill on both (which would
; make the second read a use of an already-killed register and trip
; -verify-machineinstrs with "Using an undefined physical register"). Both
; window halves must still be loaded from dr12 before the trigger.
; ---------------------------------------------------------------------------
define float @t_same(float %x) {
; MIR2-LABEL: name: t_same
; MIR2: TFPU_LD_AR %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_LD_BR %{{[0-9]+}}, implicit-def $dr0
; MIR2-NEXT: TFPU_MUL implicit-def
; ASM2-LABEL: _t_same:
; ASM2: mov r12, a
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
; ASM0-LABEL: _t_same:
; ASM0: mov dr4, dr12
; ASM0: mov dr0, dr12
; ASM0-NEXT: mov 0xed, #0x1e
; ASM0-COUNT-34: nop
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.mul(i32 %b, i32 %b)
  %f = bitcast i32 %r to float
  ret float %f
}

; ---------------------------------------------------------------------------
; Register pressure: many live values across the sqrt window. The implicit
; R0-R7 defs of the window force the allocator to spill/relocate everything
; else around the sequence; nothing live may sit inside the window at the
; trigger. The window loads still name the parking register, and the
; readback still comes straight out of dr4.
; Compiles clean under -verify-machineinstrs at both levels.
; ---------------------------------------------------------------------------
define i32 @t_pressure(i32 %a0, i32 %a1, i32 %a2, i32 %a3,
                       i32 %a4, i32 %a5, i32 %a6, i32 %a7, i32 %x) {
; MIR2-LABEL: name: t_pressure
; MIR2: TFPU_LD_AR killed %{{[0-9]+}}, implicit-def $dr4
; MIR2-NEXT: TFPU_SQRT implicit-def
; MIR2-NEXT: {{.*}} = TFPU_RD_AR implicit $dr4
; ASM2-LABEL: _t_pressure:
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x20
; ASM2-COUNT-54: nop
; The very next instruction after the wait is the in-window readback; only
; after it does the pressure-driven spill traffic resume. The window bytes
; hold only the sqrt operand/result at the trigger.
; ASM2-NEXT: mov dr12, dr4
; ASM2: mov dpl, r3
; ASM2: eret
  %s0 = add i32 %a0, %a1
  %s1 = add i32 %a1, %a2
  %s2 = add i32 %a2, %a3
  %s3 = add i32 %a3, %a4
  %s4 = add i32 %a4, %a5
  %s5 = add i32 %a5, %a6
  %s6 = add i32 %a6, %a7
  %s7 = add i32 %a7, %a0
  %r = call i32 @llvm.mcs251.tfpu.sqrt(i32 %x)
  %t0 = add i32 %s0, %s1
  %t1 = add i32 %s2, %s3
  %t2 = add i32 %s4, %s5
  %t3 = add i32 %s6, %s7
  %u0 = add i32 %t0, %t1
  %u1 = add i32 %t2, %t3
  %u2 = add i32 %u0, %u1
  %res = add i32 %u2, %r
  ret i32 %res
}
