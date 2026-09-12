; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -O2 -stop-after=finalize-isel < %s | FileCheck %s --check-prefix=MIR --implicit-check-not='implicit-def dead ${{dpxl|dpl|dph|a}}'
;
; X2-fix regression (X4 e2e defect): at -O1+ the DAG combiner legitimately
; parallelises disjoint NON-VOLATILE memory ops --
; parallelizeChainedStores for same-base disjoint stores (store3 below) and
; FindBetterChain for same/different-base ops (store2) -- so the chain
; inputs of several AS3 accesses in one block become TokenFactor-parallel.
; The DPL/DPH/DPXL/A pins of the MOVX sequence live only in the MCInstrDesc
; implicit operand lists, and the SelectionDAG schedulers do not see those
; (AddSchedEdges only follows SDValue operands), so the pre-RA list
; scheduler streamed the lane writes by register across the parallel
; accesses: dpxl xN, dpl xN, dph xN, a xN, then bare MOVX xN -- every MOVX
; then addressed through the LAST written pointer (QEMU-confirmed
; miscompile of the store3 shape).  The fix welds each MOVX byte sequence
; with Glue (buildMOVXByteLoad/buildMOVXByteStore, same device as the
; MOVXALD->MOV8ra tail and the bit-read group), so one access is ONE
; scheduling unit while independent accesses stay freely inter-ordable as
; wholes.
;
; ASM pins the invariant that matters architecturally: every MOVX is
; immediately preceded by its own DPXL/DPL/DPH lane writes (and, on the
; store side, its own A value).  The address-materialisation moves (the
; .db relocation windows and the value loads) may float around freely --
; only the SFR writes are pinned; the CHECK-NEXT chains below reject the
; streamed-by-register defect shape, under which the second and third
; matches cannot bind.
;
; MIR pins the second half of the fix: the glue chain feeds
; InstrEmitter's setPhysRegsDeadExcept scan, so the lane writes' implicit
; defs are emitted LIVE (the glued MOVX implicit uses count as uses) and
; the pre/post-RA machine schedulers keep the real physical-register
; dependencies.  Before the fix these all carried spurious "dead" markers
; that contradicted the MOVX implicit uses.

@a3 = external addrspace(3) global [3 x i8]
@x = external addrspace(3) global i8
@y = external addrspace(3) global i8

; The exact X4 defect shape: three disjoint same-base stores (the
; parallelizeChainedStores trigger).
define void @store3(i8 %v0, i8 %v1, i8 %v2) {
; ASM-LABEL: _store3:
; ASM:         mov 0x84, r[[B0:[0-9]+]]
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    mov a, r{{[0-9]+}}
; ASM-NEXT:    movx @dptr, a
; ASM:         mov 0x84, r[[B1:[0-9]+]]
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    mov a, r{{[0-9]+}}
; ASM-NEXT:    movx @dptr, a
; ASM:         mov 0x84, r[[B2:[0-9]+]]
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    mov a, r{{[0-9]+}}
; ASM-NEXT:    movx @dptr, a
; MIR-LABEL: name: store3
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
; MIR:         MOV8dpxl {{.*}} implicit-def $dpxl
; MIR:         MOVXAST implicit $a, implicit $dpl, implicit $dph, implicit $dpxl
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
  store i8 %v0, ptr addrspace(3) @a3, align 1
  store i8 %v1, ptr addrspace(3) getelementptr inbounds nuw (i8, ptr addrspace(3) @a3, i32 1), align 1
  store i8 %v2, ptr addrspace(3) getelementptr inbounds nuw (i8, ptr addrspace(3) @a3, i32 2), align 1
  ret void
}

; Two stores to DIFFERENT globals (the FindBetterChain re-chaining shape):
; same per-access invariant.
define void @store2(i8 %v0, i8 %v1) {
; ASM-LABEL: _store2:
; ASM:         mov 0x84, r{{[0-9]+}}
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    mov a, r{{[0-9]+}}
; ASM-NEXT:    movx @dptr, a
; ASM:         mov 0x84, r{{[0-9]+}}
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    mov a, r{{[0-9]+}}
; ASM-NEXT:    movx @dptr, a
; MIR-LABEL: name: store2
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
; MIR:         MOV8dpxl {{.*}} implicit-def $dpxl
; MIR:         MOVXAST implicit $a, implicit $dpl, implicit $dph, implicit $dpxl
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
  store i8 %v0, ptr addrspace(3) @x, align 1
  store i8 %v1, ptr addrspace(3) @y, align 1
  ret void
}

; Load side of the same defect: three disjoint same-base loads (summed so
; none is dead).  Before the fix the MOVXALDs all read through the last
; written pointer.
define i8 @load3() {
; ASM-LABEL: _load3:
; ASM:         mov 0x84, r{{[0-9]+}}
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    movx a, @dptr
; ASM-NEXT:    mov r{{[0-9]+}}, a
; ASM:         mov 0x84, r{{[0-9]+}}
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    movx a, @dptr
; ASM-NEXT:    mov r{{[0-9]+}}, a
; ASM:         mov 0x84, r{{[0-9]+}}
; ASM-NEXT:    mov dpl, r{{[0-9]+}}
; ASM-NEXT:    mov dph, r{{[0-9]+}}
; ASM-NEXT:    movx a, @dptr
; ASM-NEXT:    mov r{{[0-9]+}}, a
; MIR-LABEL: name: load3
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
; MIR:         MOV8dpxl {{.*}} implicit-def $dpxl
; MIR:         MOVXALD implicit-def $a, implicit $dpl, implicit $dph, implicit $dpxl
; MIR-NOT:     implicit-def dead $dpxl
; MIR-NOT:     implicit-def dead $dpl
; MIR-NOT:     implicit-def dead $dph
; MIR-NOT:     implicit-def dead $a
  %a = load i8, ptr addrspace(3) @a3, align 1
  %b = load i8, ptr addrspace(3) getelementptr inbounds nuw (i8, ptr addrspace(3) @a3, i32 1), align 1
  %c = load i8, ptr addrspace(3) getelementptr inbounds nuw (i8, ptr addrspace(3) @a3, i32 2), align 1
  %s1 = add i8 %a, %b
  %s2 = add i8 %s1, %c
  ret i8 %s2
}
