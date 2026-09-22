; RUN: split-file %s %t
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/narrow-pair.ll -o %t/a.o 2>&1 | FileCheck %s --check-prefix=N6
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/wide-offset.ll -o %t/b.o 2>&1 | FileCheck %s --check-prefix=W12
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/three-fields.ll -o %t/c.o 2>&1 | FileCheck %s --check-prefix=T9
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/bare-i8.ll -o %t/d.o 2>&1 | FileCheck %s --check-prefix=B1
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/wrong-shape-8.ll -o %t/e.o 2>&1 | FileCheck %s --check-prefix=S8
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/global-narrow.ll -o %t/f.o 2>&1 | FileCheck %s --check-prefix=G6
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vacopy-dst.ll -o %t/g.o 2>&1 | FileCheck %s --check-prefix=CD6
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/vacopy-src.ll -o %t/h.o 2>&1 | FileCheck %s --check-prefix=CS6
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/gep-subfield.ll -o %t/i.o 2>&1 | FileCheck %s --check-prefix=GS6
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/zero-alloca.ll -o %t/j.o 2>&1 | FileCheck %s --check-prefix=ZA0
; RUN: llc -mtriple=mcs251 < %t/frozen-ok.ll | FileCheck %s --check-prefix=OK
; RUN: llc -mtriple=mcs251 < %t/opaque-ok.ll | FileCheck %s --check-prefix=OP
;
; G2 B-S2 review fix (Alice blocker 1, backend half): LowerVASTART and
; LowerVACOPY write the frozen pair unconditionally -- a PtrVT-wide base
; at byte 0 and a 4-byte i32 offset at byte 4 -- so a va_start/va_copy
; aimed at a narrower or wider object was a silent out-of-bounds store and
; llc still exited 0 (the round-2 probe: `alloca {ptr, i16}` + va_start
; compiled clean).  clang pins the pair at __va_list_tag construction
; (ASTContext: the offset field is the 32-bit unsigned long, never the
; +int16-degenerated 2-byte unsigned int); these are the backend second
; gate pins.  Every case below names a COUNTABLE object (an alloca or a
; global) whose exact shape is wrong:
;
;   N6  the +int16 degeneration itself: {ptr, i16} is 6 bytes, the
;       fixed byte-4 4-byte offset store runs 2 bytes past its end.
;   W12 a widened offset field {ptr, i64} -- the copy/anchor writes still
;       touch only 8 of the 12 bytes, but the consumers' hardwired byte-4
;       access and one-slot advance break the pair contract.
;   T9  a third field {ptr, i32, i8}: the pair is no longer two fields.
;   B1  a bare byte object: no pair at all.
;   S8  exactly 8 bytes but NOT the pair ({i32, i32}: no pointer base) --
;       the check is a shape check, not a size check.
;   G6  the object is a global, not an alloca -- the same shape check
;       reaches it through the GlobalVariable branch.
;   CD6 va_copy with a narrow DESTINATION: the copy's second store takes
;       the same 2-byte out-of-bounds write.
;   CS6 va_copy with a narrow SOURCE.
;   OK  the two shapes the gate must keep accepting: clang's actual
;       `[1 x %struct.__va_list_tag]` array wrapper (unwrapped to the
;       two-field pair) with va_start AND va_copy lowering cleanly.
;   OP  the boundary the gate cannot count: a va_start aimed at a pointer
;       ARGUMENT names no countable object (no alloca/global to measure),
;       so the backend stays quiet -- the producer-side pin (clang's
;       ASTContext __va_list_tag construction) owns that path.  Pinned
;       here so the gate is known not to false-positive on opaque
;       pointers.
;
; va_end needs no gate: LowerOperation lowers VAEND to the bare chain (a
; no-op -- the pair lives in the owner's frame), so a narrow va_end alone
; cannot corrupt anything.

; N6: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 6-byte object ({ ptr, i16 })
; W12: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 12-byte object ({ ptr, i64 })
; T9: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 9-byte object ({ ptr, i32, i8 })
; B1: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 1-byte object (i8)
; S8: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 8-byte object ({ i32, i32 })
; G6: LLVM ERROR: MCS251: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 6-byte object ({ ptr, i16 })
; CD6: LLVM ERROR: MCS251: va_copy destination requires the frozen 8-byte va_list pair {ptr, i32}; got a 6-byte object ({ ptr, i16 })
; CS6: LLVM ERROR: MCS251: va_copy source requires the frozen 8-byte va_list pair {ptr, i32}; got a 6-byte object ({ ptr, i16 })

;--- narrow-pair.ll
declare void @llvm.va_start.p0(ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %vl = alloca {ptr, i16}
  call void @llvm.va_start.p0(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- wide-offset.ll
declare void @llvm.va_start.p0(ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %vl = alloca {ptr, i64}
  call void @llvm.va_start.p0(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- three-fields.ll
declare void @llvm.va_start.p0(ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %vl = alloca {ptr, i32, i8}
  call void @llvm.va_start.p0(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- bare-i8.ll
declare void @llvm.va_start.p0(ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %vl = alloca i8
  call void @llvm.va_start.p0(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- wrong-shape-8.ll
declare void @llvm.va_start.p0(ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %vl = alloca {i32, i32}
  call void @llvm.va_start.p0(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- global-narrow.ll
declare void @llvm.va_start.p0(ptr)
@gvl = global {ptr, i16} zeroinitializer
define void @f(i32 %n, ...) addrspace(4) {
  call void @llvm.va_start.p0(ptr @gvl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- vacopy-dst.ll
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %a = alloca {ptr, i32}
  %b = alloca {ptr, i16}
  call void @llvm.va_start.p0(ptr %a)
  call void @llvm.va_copy.p0(ptr %b, ptr %a)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- vacopy-src.ll
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
define void @f(i32 %n, ...) addrspace(4) {
  %a = alloca {ptr, i32}
  %b = alloca {ptr, i32}
  %c = alloca {ptr, i16}
  call void @llvm.va_start.p0(ptr %a)
  call void @llvm.va_copy.p0(ptr %b, ptr %c)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}

;--- frozen-ok.ll
; The shapes the gate keeps: clang's __va_list_tag[1] wrapper (8 bytes,
; unwrapped to the two-field pair) and the bare 8-byte struct form; both
; va_start and va_copy lower through it.
%struct.__va_list_tag = type { ptr, i32 }
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)
define i32 @g(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 1
  %ap2 = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_copy.p0(ptr %ap2, ptr %ap)
  %offp2 = getelementptr inbounds i8, ptr %ap2, i32 4
  %off = load i32, ptr %offp2, align 1
  %base = load ptr, ptr %ap2, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load i32, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp2, align 1
  call void @llvm.va_end.p0(ptr %ap2)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %v
}
!mcs251.signatures = !{!0}
!0 = !{!"_g", i32 9, i32 0, i32 0}
; OK: .globl _g_PARM_2
; OK: _g:
; The va_start anchor plus its two 4-byte pair stores: the slot-symbol
; base at +0 and the zeroed i32 offset at +4.
; OK: .db 0x7e, 0x08, (_g_PARM_2) >> 8, (_g_PARM_2)
; OK: .db 0x7a, 0x0c, 0x00, (_g_PARM_2) >> 16
; OK: mov @dr60-0x000f, r0
; OK: mov @dr60-0x000c, r3
; OK: mov r4, #0x00
; OK: mov @dr60-0x000b, r4
; OK: mov @dr60-0x0008, r4
; The live va_copy: both fields of the source pair reloaded, both stored
; into the copy (bytes -0x0007..-0x0004 = the destination pair).
; OK: mov r4, @dr60-0x000f
; OK: mov r8, @dr60-0x000b
; OK: mov r12, @dr60-0x0009
; OK: mov @dr60-0x0007, r4
; OK: mov @dr60-0x0004, r7
; The slot read goes through the COPIED pair's base+off.
; OK: mov r0, @dr60-0x0003
; OK: add dr12, dr0
; OK: mov r4, @dr12

;--- opaque-ok.ll
declare void @llvm.va_start.p0(ptr)
define void @f(ptr %external, i32 %n, ...) addrspace(4) {
  call void @llvm.va_start.p0(ptr %external)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 9, i32 0, i32 0}
; OP: .globl _f_PARM_3
; OP: _f:

; A GEP that lands on a nested sub-object redirects the shape check to the
; sub-object: the 6-byte inner pair is judged, not the 12-byte container.
;--- gep-subfield.ll
declare void @llvm.va_start(ptr)
define void @f(i32 %n, ...) {
  %a = alloca {i8, {ptr, i16}}
  %inner = getelementptr {i8, {ptr, i16}}, ptr %a, i32 0, i32 1
  call void @llvm.va_start(ptr %inner)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 1, i32 1}
; GS6: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 6-byte object ({ ptr, i16 })

; The array-size operand is honored: a zero-element alloca of the right
; element type allocates nothing and must not pass.
;--- zero-alloca.ll
declare void @llvm.va_start(ptr)
define void @f(i32 %n, ...) {
  %vl = alloca {ptr, i32}, i32 0
  call void @llvm.va_start(ptr %vl)
  ret void
}
!mcs251.signatures = !{!0}
!0 = !{!"_f", i32 1, i32 1}
; ZA0: va_start requires the frozen 8-byte va_list pair {ptr, i32}; got a 0-byte object ({ ptr, i32 })
