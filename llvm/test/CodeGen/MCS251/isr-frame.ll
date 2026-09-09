; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O2 -verify-machineinstrs %s -o - | FileCheck %s

; ISR campaign T05: LowerReturn RETI split + the A6 fixed 37B frame.
;
; Text form per the 2026-09-09 Alice ruling: explicit
; -mcs251-memory-contract=1,2,32,8,1, ISR definitions in addrspace(4), and
; llvm.used keepalive roots in the standard addrspacecast form.
;
; Integration note: the full RUN lines above (text assembly and, for the
; companion object runs, ELF emission) are blocked behind T06 step 8 -- the
; exact used-root exemption in the AsmPrinter v1 finalization gate -- exactly
; like T04's isr-instructions.mir and T06's isr-object.ll. Until T06 lands,
; the stage-acceptance evidence for this file is the pre-AsmPrinter run:
;   llc -mtriple=mcs251 -mcs251-memory-contract=1,2,32,8,1 -O0 -verify-machineinstrs -stop-before=mcs251-asm-printer %s -o %t.pre-asm.mir
;
; SPX ledger for _irq at entry value S (37B accounted separately from the
; 12-byte local frame, per A6):
;   S                     entry
;   +4                    hardware interrupt frame (profile 1, before 1st push)
;   +1 +4*9  = +37 -> S+41   the ten FrameSetup pushes
;   +12      = +F  -> S+41+F  the three FrameSetup inc-spx steps
;   -12             -> S+41    the three FrameDestroy dec-spx steps
;   -4*9 -1  = -37 -> S+4     the ten FrameDestroy pops (pop psw last)
;   RETI            -> S      hardware frame pop
; The 37B save area never enters MachineFrameInfo::StackSize; the local
; objects still use the ObjectOffset - StackSize displacement form.

target triple = "mcs251-unknown-none"

@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr), ptr addrspacecast (ptr addrspace(4) @irq2 to ptr)], section "llvm.metadata"

declare void @helper(ptr)
@flag = global i8 0

define mcs251_intrcc void @irq() addrspace(4) #0 {
entry:
  %a = alloca [12 x i8], align 1
  call void @helper(ptr %a)
  ret void
}

define void @ordinary() {
  ret void
}

; Two return blocks: every ISR exit must carry the complete inverse restore
; and end in RETI (early-return coverage pending T09; this is the T05
; self-test minimum).
define mcs251_intrcc void @irq2() addrspace(4) #1 {
entry:
  %v = load volatile i8, ptr @flag
  %z = icmp eq i8 %v, 0
  br i1 %z, label %a, label %b
a:
  store volatile i8 1, ptr @flag
  ret void
b:
  store volatile i8 2, ptr @flag
  ret void
}

attributes #0 = { noinline "mcs251-isr-vector"="1" }
attributes #1 = { noinline "mcs251-isr-vector"="2" }

; CHECK-LABEL: _irq:
; CHECK: push psw
; CHECK-NEXT: push dr0
; CHECK-NEXT: push dr4
; CHECK-NEXT: push dr8
; CHECK-NEXT: push dr12
; CHECK-NEXT: push dr16
; CHECK-NEXT: push dr20
; CHECK-NEXT: push dr24
; CHECK-NEXT: push dr28
; CHECK-NEXT: push dpx
; CHECK: inc spx
; CHECK: ecall _helper
; CHECK: dec spx
; CHECK: pop dpx
; CHECK-NEXT: pop dr28
; CHECK-NEXT: pop dr24
; CHECK-NEXT: pop dr20
; CHECK-NEXT: pop dr16
; CHECK-NEXT: pop dr12
; CHECK-NEXT: pop dr8
; CHECK-NEXT: pop dr4
; CHECK-NEXT: pop dr0
; CHECK-NEXT: pop psw
; CHECK-NEXT: reti
; CHECK-LABEL: _ordinary:
; CHECK: eret
; CHECK-LABEL: _irq2:
; CHECK: push psw
; CHECK-NOT: eret
; CHECK: pop psw
; CHECK-NEXT: reti
; CHECK-NOT: eret
