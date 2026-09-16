; G11-B: bind carriers survive the optimization pipeline and reach the
; NOTE table (coordinator pre-ruling 2026-09-16: the emitter keeps every
; bind external declaration alive in llvm.compiler.used -- an internal
; emission mechanism, no user-visible keepalive semantics; mcu_retain keeps
; its place_at-definition-only boundary).
;
; The module carries one REFERENCED bind (bindobj, used by the function)
; and two UNREFERENCED binds (ghost, ghostx -- the O2-deletion shapes).
; A bind record owns no storage and no section: ownership=bind, size =
; the declared sizeof for objects (0 for functions), address A verbatim.
; The keepalive container itself is consumed as identity-not-bytes: no
; llvm.compiler.used bytes, relocations or sections appear in the object.
;
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %s -o %t.o2
; RUN: llvm-objcopy --dump-section=.mcs251.placement=%t.note %t.o
; RUN: llvm-objcopy --dump-section=.mcs251.placement=%t.o2.note %t.o2
; The placement contract is optimization-invariant: byte-for-byte equal
; NOTE tables at O0 and O2 (all records are attribute-derived constants).
; RUN: cmp %t.note %t.o2.note
; RUN: %python %S/Inputs/check-placement-note.py %t.o2 \
; RUN:   keep    0 0 0 0x50     4 1 1 \
; RUN:   plain   0 0 0 0x58     4 1 0 \
; RUN:   bindobj 0 0 1 0x2A     2 1 0 \
; RUN:   ghost   0 0 1 0x60     1 1 0 \
; RUN:   ghostx  1 0 1 0x10008  4 1 0 \
; RUN:   bindfn  2 1 1 0xFC4000 0 1 0
; RUN: llvm-readobj --sections --symbols --relocations %t.o2 | FileCheck %s --check-prefix=ELF --implicit-check-not=llvm.compiler.used
; Only the two owned objects own sections; the bind carriers have none.
; ELF: Name: .mcu.fixed.keep
; ELF: SHF_GNU_RETAIN (0x200000)
; ELF: Name: .mcu.fixed.plain
; ELF-NOT: .mcu.fixed
; ELF: Name: .mcs251.placement
; The bind references resolve through the ordinary undefined-symbol
; channels: the AS0 object through the byte-of-24 fields, the bind
; function call through R_MCS251_24.
; ELF: R_MCS251_HI8 _bindobj
; ELF: R_MCS251_24 _bindfn

target triple = "mcs251-unknown-none"

@keep = global i32 0, align 1 #0
@plain = global i32 9, align 1 #1
@bindobj = external global i16 #2
@ghost = external global i8 #3
@ghostx = external addrspace(3) global i32 #4

declare i32 @bindfn(i32) addrspace(4) #5

define i32 @caller(i32 %a) addrspace(4) {
entry:
  %v = load volatile i16, ptr @bindobj, align 1
  %e = zext i16 %v to i32
  %r1 = call addrspace(4) i32 @bindfn(i32 %e)
  %r2 = add i32 %r1, %a
  store volatile i32 %r2, ptr @keep, align 1
  ret i32 %r2
}

!mcs251.signatures = !{!10000, !10001}
!10000 = !{!"_caller", i32 1, i32 0, i32 0}
!10001 = !{!"_bindfn", i32 2, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0x50,data,object,owned,1" "mcs251-stable-symbol"="keep" }
attributes #1 = { "mcs251-place"="0x58,data,object,owned,0" "mcs251-stable-symbol"="plain" }
attributes #2 = { "mcs251-place"="0x2A,data,object,bind,0" "mcs251-stable-symbol"="bindobj" }
attributes #3 = { "mcs251-place"="0x60,data,object,bind,0" "mcs251-stable-symbol"="ghost" }
attributes #4 = { "mcs251-place"="0x10008,xdata,object,bind,0" "mcs251-stable-symbol"="ghostx" }
attributes #5 = { "mcs251-place"="0xFC4000,code,function,bind,0" "mcs251-stable-symbol"="bindfn" }
