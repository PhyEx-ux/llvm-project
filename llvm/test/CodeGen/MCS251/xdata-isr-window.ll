; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 < %s | FileCheck %s
;
; X2-4 regression: the interrupt window for AS3 (`__xdata`) accesses.  The
; self-healing per-access "mov 0x84" region write only protects sequence
; boundaries -- an interrupt landing INSIDE an access sequence (region byte
; already written, movx still pending) must not leak the ISR's re-point into
; the interrupted context (Alice's interrupt-trace counterexample: mainline
; bank 01 preempted mid-sequence, ISR re-points to bank 02, interrupted MOVX
; would hit 02:1234 instead of 01:1234).  The A6 fixed ISR frame therefore
; SAVES AND RESTORES DPXL -- push dpx is the last FrameSetup save, pop dpx
; the first FrameDestroy restore -- and the modelling pins the frame for the
; optimizer: ISR_PUSH_DPX Uses DPXL, ISR_POP_DPX Defs DPXL, DPXL is an ISR
; async live-in (MCS251InstrInfo.td / MCS251FrameLowering.cpp).
;
; Pinned here, in ONE module where the bank-01 mainline and the bank-02 ISR
; preempt each other:
;   * the ISR body's region writes and movx's sit strictly INSIDE the
;     push dpx / pop dpx window -- a future optimisation that deletes the
;     frame or moves an access across its boundary breaks these lines;
;   * every access still re-points DPXL immediately before its movx (the
;     window pair and the ISR frame coexist).
;
; Retention protocol, three separate layers (design supplement §3, X2-4):
; CRT has no initial-value obligation, ordinary callees may clobber DPXL
; freely (the next access self-heals), only async preemption carries the
; save/restore duty; a user's own write to SFR 0x84 stays a documented
; undefined interaction and does not exempt the compiler's ISR duty.

@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"

; Mainline: volatile AS3 read-modify-write of 0x011234 (bank 01h, the DPXL
; reset bank).  Each access carries its own region write before its movx.
define void @win_main() {
; CHECK-LABEL: _win_main:
; CHECK:         mov r[[MB:[0-9]+]], #0x01
; CHECK-NEXT:    mov 0x84, r[[MB]]
; CHECK:         movx a, @dptr
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         eret
  %p1 = inttoptr i32 70196 to ptr addrspace(3)
  %v = load volatile i8, ptr addrspace(3) %p1
  %p2 = inttoptr i32 70196 to ptr addrspace(3)
  store volatile i8 %v, ptr addrspace(3) %p2
  ret void
}

; ISR: volatile AS3 stores to 0x021234 (bank 02h).  push dpx precedes the
; FIRST region write and pop dpx follows the LAST movx.
define mcs251_intrcc void @irq() addrspace(4) #0 {
; CHECK-LABEL: _irq:
; CHECK:         push psw
; CHECK-NEXT:    push dr0
; CHECK-NEXT:    push dr4
; CHECK-NEXT:    push dr8
; CHECK-NEXT:    push dr12
; CHECK-NEXT:    push dr16
; CHECK-NEXT:    push dr20
; CHECK-NEXT:    push dr24
; CHECK-NEXT:    push dr28
; CHECK-NEXT:    push dpx
; CHECK:         mov r[[IB:[0-9]+]], #0x02
; CHECK:         mov 0x84, r[[IB]]
; CHECK:         movx @dptr, a
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK-NOT:     movx
; CHECK-NOT:     mov 0x84
; CHECK:         pop dpx
; CHECK-NEXT:    pop dr28
; CHECK-NEXT:    pop dr24
; CHECK-NEXT:    pop dr20
; CHECK-NEXT:    pop dr16
; CHECK-NEXT:    pop dr12
; CHECK-NEXT:    pop dr8
; CHECK-NEXT:    pop dr4
; CHECK-NEXT:    pop dr0
; CHECK-NEXT:    pop psw
; CHECK-NEXT:    reti
  %p = inttoptr i32 135732 to ptr addrspace(3)
  store volatile i8 7, ptr addrspace(3) %p
  %q = inttoptr i32 135732 to ptr addrspace(3)
  store volatile i8 8, ptr addrspace(3) %q
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }
