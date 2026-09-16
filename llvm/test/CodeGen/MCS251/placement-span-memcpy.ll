; G11-B R3 (review 2026-09-16 §三): the span-memcpy fixture of the design's
; four-function-span matrix (§5, probe4 obligation N2-2) -- a placed function
; whose body drives the TARGET INLINE/SHRINK path of llvm.memcpy: the call is
; expanded by instruction selection into an inline load/store sequence (never
; a libcall: no _memcpy symbol exists anywhere in the object -- the
; relocations of the fixed section only reference the parameter slot), and
; the -O2 shrink of that sequence is a pure body-byte effect.
;
; The span identities are hard assertions from the real object:
;   NOTE.size == sh_size >= st_size   (equality: no same-section payload)
; with NOTE.size produced by the <stable>.end - <stable>.begin MC symbol
; difference, i.e. the post-layout value including every expanded byte.
;
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --symbols --relocations %t.o | FileCheck %s --check-prefix=ELF --implicit-check-not=_memcpy
; ELF: Name: .mcu.fixed.mcp
; ELF: Type: SHT_PROGBITS
; ELF: Flags [ (0x6)
; ELF: Size: 227
; The inline expansion never leaves a call behind: the only relocations of
; the entity's section are the parameter-slot loads.
; ELF: R_MCS251_MID8 _mcp_PARM_2
; ELF: R_MCS251_LO8 _mcp_PARM_2
; ELF: R_MCS251_HI8 _mcp_PARM_2
; ELF: Name: _mcp
; ELF: Value: 0x0
; ELF: Size: 227
; ELF: Type: Function
; ELF: Section: .mcu.fixed.mcp
;
; NOTE.size == sh_size == 227 decoded and re-hashed by the independent
; oracle (a FAIL, never a SKIP; printed sizes are not acceptance).
; RUN: %python %S/Inputs/check-placement-note.py %t.o mcp 2 1 0 0xFC7000 227 1 0

target triple = "mcs251-unknown-none"

define void @mcp(ptr %d, ptr %s) addrspace(4) #0 {
entry:
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %d, ptr align 4 %s, i32 16, i1 false)
  ret void
}

declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)

!mcs251.signatures = !{!10000}
!10000 = !{!"_mcp", i32 2, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0xFC7000,code,function,owned,0" "mcs251-stable-symbol"="mcp" }
