; RUN: split-file %s %t
; BRJT review round 2 (H1/REL): with the gate open, a jump table is a
; column of R_MCS251_J16 code-address fields. The frozen REL object writer
; carries the 16/24/lo8/mid8/hi8 modes only -- no J16 R-mode -- so REL object
; output must be rejected with the explicit jump-table diagnostic, never left
; to the REL writer's generic "expected relocatable expression / unsupported
; relocation kind" errors (the REL streamer reports hasRawTextSupport()==true,
; so the guard must decide on the object output mode, not on raw-text
; support). Assembly text stays legal: it spells the same table bytes
; literally.
;
; RUN: not --crash llc -mtriple=mcs251 -O0 -mcs251-jump-tables -filetype=obj %t/jt.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=RELOBJ
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables -mcs251-object-format=elf -filetype=obj %t/jt.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/jt.ll -o - | FileCheck %s --check-prefix=ASMTEXT

; RELOBJ: LLVM ERROR: MCS251 jump tables require ELF object output; the REL object writer has no J16 code-address relocation (-filetype=obj -mcs251-object-format=elf)

; ASMTEXT: mov dptr, #.LJTI
; ASMTEXT-NEXT: jmp @a+dptr
; ASMTEXT: .LJTI0_0:
; ASMTEXT-NEXT: .byte 2
; ASMTEXT-NEXT: .byte .LBB0_1>>8
; ASMTEXT-NEXT: .byte .LBB0_1

;--- jt.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@g = external dso_local global i32, align 1

; N=4 dense, Range=4: E2/E3/E5/E4 all qualified -- the table IS built when
; the gate is open.
define dso_local void @f(i32 noundef %x) {
entry:
  switch i32 %x, label %d [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  store volatile i32 10, ptr @g, align 1
  ret void
c1:
  store volatile i32 11, ptr @g, align 1
  ret void
c2:
  store volatile i32 12, ptr @g, align 1
  ret void
c3:
  store volatile i32 13, ptr @g, align 1
  ret void
d:
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_f", i32 1, i32 0}
