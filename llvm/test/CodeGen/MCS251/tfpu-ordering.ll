; G7 S3 (G7-FLOAT-DESIGN-draft.md §2.4 "副作用与 TPIN 顺序"): the demo 38
; TPIN pin toggle brackets the TFPU sequence as a timing window -- the
; optimization must never move ANY step of the hardware sequence out of the
; window, at either opt level.
;
; STRICT form (Alice's ruling): the COMPLETE window --
;
;   window load (mov dr4, dr12)  ->  trigger (mov 0xed, #cmd)
;     ->  fixed wait chain        ->  readback (mov <outside-reg>, dr4)
;     ->  every consumer of the result
;
; must all sit between the TPIN=0 write (`setb 0x90`) and the TPIN=1 write
; (`clr 0x90`). Two earlier bugs made a step escape:
;
;   * the window load hoisting ahead of `setb` -- the operand vreg coalesced
;     into dr4 and the gather stayed at the function top (blocker 1, operand
;     half). Forbidden outright: `mov dr4, dr12` must be the first window
;     write and must come AFTER the window opens.
;   * the readback sinking past `clr` -- getCopyFromReg(DR4) lowered to a
;     COPY the allocator folded away, so the first real read of the dr4 lanes
;     became the consumer (`mov dpl, r7` / the store) AFTER the window closed
;     (blocker 1, readback half). The readback is now the post-RA TFPU_RD_AR
;     pseudo, expanded to `mov <outside-reg>, dr4` right after the wait chain,
;     and the assertions below pin it before `clr`.
;
; The readback destination is dr12 (outside both window halves) when it is
; free, or dr0 (the BR half, unused by a unary command and dead once a binary
; trigger has fired) when dr12 still holds a live operand/pointer -- see
; GPR32Rd in MCS251RegisterInfo.td. Either is correct; both are pinned, and
; neither may be dr4 (a read into dr4 is the no-op that restores the bug).
;
; The readback shapes are pinned in all three consumer forms the review asked
; for: return value (ret_shape), softened store through a pointer (tpin), and
; the binary four (bin_shape).
;
; The IR intrinsic is [IntrHasSideEffects] (worst-case memory + other side
; effects) and the window-load / trigger / readback pseudos carry Chain+Glue,
; so nothing can sink or hoist across the bit writes.
;
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 %s -o - | FileCheck %s --check-prefix=ASM0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 %s -o - | FileCheck %s --check-prefix=ASM2

target triple = "mcs251"

declare void @llvm.mcs251.bit.set(i32 immarg)
declare void @llvm.mcs251.bit.clear(i32 immarg)
declare i32 @llvm.mcs251.tfpu.sin(i32)
declare i32 @llvm.mcs251.tfpu.mul(i32, i32)

; TPIN = bit 0x90 (an SFR-space bit, the sbit style of demo 38).
; Source order: window open -> sin -> window close. The checked order is
; the emitted instruction order: setb 0x90 ... 75 ED 2D ... readback ... clr.
; The result is stored through the pointer parameter (a softened f32 store
; of the bit pattern); the data gate for float GLOBALS is G7-S2's scope and
; deliberately not exercised here.
define void @tpin(float %x, ptr %out) {
; ASM0-LABEL: _tpin:
; The AR window load, the trigger and the readback must NOT appear before the
; window opens. Everything the coprocessor sees (including the read) is
; inside.
; ASM0-NOT: mov dr4, dr12
; ASM0-NOT: mov 0xed
; ASM0-NOT: mov dr{{0|12}}, dr4
; ASM0: setb 0x90
; The window load is inside, then the trigger, then the whole fixed worst-case
; chain.
; ASM0-NEXT: mov dr4, dr12
; ASM0-NEXT: mov 0xed, #0x2d
; ASM0-COUNT-270: nop
; The in-window readback then snapshots the dr4 result into the parking
; register (spill reloads may sit between the wait chain and it at -O0; what
; matters is that the read is before `clr`), and the store consumes the
; SNAPSHOT, still inside the window.
; ASM0: mov dr{{0|12}}, dr4
; ASM0: mov @dr0, {{r[0-9]+}}
; ASM0: mov @dr0+0x0003, {{r[0-9]+}}
; ASM0: clr 0x90
; ASM0: eret
;
; ASM2-LABEL: _tpin:
; Same strict form. Between `setb` and the window load the allocator may
; reload the parking register dr12 from a spill slot -- that is NOT a window
; write (dr12 is outside r0-r7), so it is allowed; what is forbidden is any
; write into the AR window, any trigger, or any readback before the window
; opens.
; ASM2-NOT: mov dr4, dr12
; ASM2-NOT: mov 0xed
; ASM2-NOT: mov dr{{0|12}}, dr4
; ASM2: setb 0x90
; ASM2: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2d
; ASM2-COUNT-270: nop
; Here the readback lands in dr0, which frees dr4 for the store pointer; the
; lanes are consumed from the snapshot inside the window, before `clr`.
; ASM2-NEXT: mov dr0, dr4
; ASM2: mov @dr4, r0
; ASM2-NEXT: mov @dr4+0x0001, r1
; ASM2-NEXT: mov @dr4+0x0002, r2
; ASM2-NEXT: mov @dr4+0x0003, r3
; ASM2-NEXT: clr 0x90
; ASM2: eret
entry:
  call void @llvm.mcs251.bit.set(i32 144)
  %b = bitcast float %x to i32
  %r = call i32 @llvm.mcs251.tfpu.sin(i32 %b)
  %f = bitcast i32 %r to float
  store float %f, ptr %out
  call void @llvm.mcs251.bit.clear(i32 144)
  ret void
}

; ---------------------------------------------------------------------------
; The reviewer's /tmp/rdbk.ll shape: set -> sin -> clear -> RETURN. Here the
; result's only consumer is the epilogue, so the pre-fix readback (a folded
; COPY from dr4) put the actual lane reads AFTER `clr 0x90`:
;
;   ... 270 x nop / clr 0x90 / mov dpl, r7 / mov dph, r6 / ...
;
; The fix snaps the window into the parking register inside the window; the
; epilogue reads the snapshot.
; ---------------------------------------------------------------------------
define i32 @ret_shape(i32 %x) {
; ASM0-LABEL: _ret_shape:
; ASM0-NOT: mov 0xed
; ASM0-NOT: mov dr{{0|12}}, dr4
; ASM0: setb 0x90
; ASM0-NEXT: mov dr4, dr12
; ASM0-NEXT: mov 0xed, #0x2d
; ASM0-COUNT-270: nop
; The readback is INSIDE: the very next instruction after the wait, and the
; window has not closed yet.
; ASM0-NEXT: mov dr12, dr4
; ASM0-NEXT: clr 0x90
; ...and there is no second, post-window read of the dr4 lanes.
; ASM0-NOT: mov dr{{0|12}}, dr4
;
; ASM2-LABEL: _ret_shape:
; ASM2-NOT: mov 0xed
; ASM2-NOT: mov dr{{0|12}}, dr4
; ASM2: setb 0x90
; ASM2-NEXT: mov dr4, dr12
; ASM2-NEXT: mov 0xed, #0x2d
; ASM2-COUNT-270: nop
; ASM2-NEXT: mov dr12, dr4
; ASM2-NEXT: clr 0x90
; The epilogue reads the dr12 snapshot (not the live dr4 lanes).
; ASM2-NEXT: mov dpl, r15
; ASM2-NEXT: mov dph, r14
; ASM2-NEXT: mov b, r13
; ASM2-NEXT: mov a, r12
; ASM2-NEXT: eret
entry:
  call void @llvm.mcs251.bit.set(i32 144)
  %r = call i32 @llvm.mcs251.tfpu.sin(i32 %x)
  call void @llvm.mcs251.bit.clear(i32 144)
  ret i32 %r
}

; ---------------------------------------------------------------------------
; The binary variant of the same shape: both window halves are loaded, then
; the trigger, the wait and the in-window readback, all before `clr`.
; ---------------------------------------------------------------------------
define i32 @bin_shape(i32 %x, i32 %y) {
; ASM0-LABEL: _bin_shape:
; ASM0-NOT: mov dr0, dr12
; ASM0-NOT: mov dr{{0|12}}, dr4
; ASM0: setb 0x90
; ASM0: mov dr4, dr12
; ASM0: mov dr0, dr12
; ASM0-NEXT: mov 0xed, #0x1e
; ASM0-COUNT-34: nop
; ASM0-NEXT: mov dr12, dr4
; ASM0-NEXT: clr 0x90
;
; ASM2-LABEL: _bin_shape:
; ASM2-NOT: mov dr0, dr12
; ASM2-NOT: mov dr{{0|12}}, dr4
; ASM2: setb 0x90
; ASM2: mov dr4, dr12
; ASM2: mov dr0, dr12
; ASM2-NEXT: mov 0xed, #0x1e
; ASM2-COUNT-34: nop
; ASM2-NEXT: mov dr12, dr4
; ASM2-NEXT: clr 0x90
; ASM2-NEXT: mov dpl, r15
; ASM2-NEXT: mov dph, r14
; ASM2-NEXT: mov b, r13
; ASM2-NEXT: mov a, r12
; (the PARM_2 stack frame is torn down before the return)
; ASM2: dec spx, #0x4
; ASM2: eret
entry:
  call void @llvm.mcs251.bit.set(i32 144)
  %r = call i32 @llvm.mcs251.tfpu.mul(i32 %x, i32 %y)
  call void @llvm.mcs251.bit.clear(i32 144)
  ret i32 %r
}
