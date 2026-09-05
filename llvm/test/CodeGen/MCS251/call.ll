; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s

; Phase 7: direct calls. Every call is `ecall _sym` (9A + addr24, 4 bytes;
; the sdas251/linker resolves the symbol to a 24-bit address, so nothing is
; ever truncated). Arguments reuse the Phase 4 ABI slots (i8 -> dpl,
; i16 -> the dpl:dph pair) and return values the return slots -- which are
; the same registers. A call result feeding the caller's own return
; therefore needs no move at all: the phys->virt->phys chain coalesces away
; (dpl/dptr are reserved), giving the SDCC-equivalent `ecall; eret`
; pass-through. Known limitation: a value live ACROSS a call cannot stay in
; any register (every GPR is caller-saved), so it must be spilled, and
; spilling needs the frame (Phase 9) -- such functions fail loudly.

declare void @pv()
declare i8 @g8()
declare i16 @g16()
declare i8 @g8p(i8)
declare i16 @g16p(i16)

define void @call_void() {
; CHECK-LABEL: _call_void:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    ecall _pv
; CHECK-NEXT:    eret
  call void @pv()
  ret void
}

define i8 @ret8_fwd() {
; The i8 result arrives in dpl and is returned in dpl: CopyFromReg(DPL) ->
; CopyToReg(DPL) fully coalesces, no intermediate move.
; CHECK-LABEL: _ret8_fwd:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    ecall _g8
; CHECK-NEXT:    eret
  %r = call i8 @g8()
  ret i8 %r
}

define i16 @ret16_inc() {
; i16 result in the dpl:dph pair; the pair is never a move operand, so the
; lanes are read out individually (dpl = low byte first), incremented as a
; word, and written back.
; CHECK-LABEL: _ret16_inc:
; CHECK:         ecall _g16
; CHECK:         mov r{{[0-9]+}}, dpl
; CHECK:         mov r{{[0-9]+}}, dph
; CHECK:         add wr{{[0-9]+}}, #0x0001
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         eret
  %r = call i16 @g16()
  %t = add i16 %r, 1
  ret i16 %t
}

define i8 @call_arg8(i8 %a) {
; The argument is computed in the caller, so the dpl pass-through breaks:
; read the incoming argument, add, load it into dpl, then ecall. The result
; pass-through makes the tail another ecall; eret pair.
; CHECK-LABEL: _call_arg8:
; CHECK:         mov r{{[0-9]+}}, dpl
; CHECK:         add r{{[0-9]+}}, #0x01
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         ecall _g8p
; CHECK:         eret
  %t = add i8 %a, 1
  %r = call i8 @g8p(i8 %t)
  ret i8 %r
}

define i16 @call_arg16(i16 %a) {
; Same at 16 bits: lanes in, word add, lanes out into dpl/dph, ecall.
; CHECK-LABEL: _call_arg16:
; CHECK:         mov r{{[0-9]+}}, dpl
; CHECK:         mov r{{[0-9]+}}, dph
; CHECK:         add wr{{[0-9]+}}, #0x0003
; CHECK:         mov dpl, r{{[0-9]+}}
; CHECK:         mov dph, r{{[0-9]+}}
; CHECK:         ecall _g16p
; CHECK:         eret
  %t = add i16 %a, 3
  %r = call i16 @g16p(i16 %t)
  ret i16 %r
}

define i16 @fwd16(i16 %a) {
; Full forwarding: the incoming argument is live in as dptr, the outgoing
; argument is loaded into dptr, the result comes back through dptr and is
; returned through dptr -- the whole register chain coalesces, exactly like
; the id16 pass-through of Phase 4. No machine instruction survives besides
; the call itself.
; CHECK-LABEL: _fwd16:
; CHECK-NEXT:  ; %bb.0:
; CHECK-NEXT:    ecall _g16p
; CHECK-NEXT:    eret
  %r = call i16 @g16p(i16 %a)
  ret i16 %r
}
