; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/neg.ll -o - | FileCheck %s --check-prefix=NONEG
; RUN: not --crash llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/i64.ll -o - 2>&1 | FileCheck %s --check-prefix=I64
; RUN: not --crash llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/i64int.ll -o /dev/null
; RUN: not --crash llc -mtriple=mcs251 -O0 %t/i64int.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/i1.ll -o - | FileCheck %s --check-prefix=I1
; RUN: llc -mtriple=mcs251 -O0 -mcs251-jump-tables %t/pos.ll -o - | FileCheck %s --check-prefix=POS
;
; The negative RUN is -O0 only: findJumpTables performs ONE whole-switch
; qualification there (exactly the P9 probe methodology), so a refused
; cluster means no table anywhere. At -O1+ the Kannan-Proebsting splitter
; re-partitions the same cases into individually-qualified dense segments
; (per-partition E3/E5 still enforced -- see switch-jt-mixed.ll); a
; >86-entry table can never exist in either mode.
;
; BRJT S3 (design S3.2.6 rev3, H4): qualification-predicate negatives. With
; `-mcs251-jump-tables` ON, every cluster below fails isSuitableForJumpTable
; and falls back to the comparison chain: no dispatch, no table column, no
; J16 fields. The rev3 crux rows are the size-attribute ones: upstream's
; base check short-circuits its hard cap under optsize/minsize
; (TargetLoweringBase.cpp "OptForSize ||", measured by probes P9-C/P9-D),
; so the backend-independent E3 check must refuse Range > 86 for BOTH
; attribute tiers -- and the positive control proves the optsize tier is not
; over-refused (optsize + dense Range=40 builds the table, density tier 40
; in force).
;--- neg.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

; H4-1: N=3 < getMinimumJumpTableEntries() (E4, upstream gate).
define void @h4_few_cases(i32 noundef %x) {
; CHECK-LABEL: h4_few_cases:
; NONEG-NOT: {{\.LJTI}}
; NONEG-NOT: jmp @a+dptr
; NONEG: cmp dr0, dr4
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; H4-2: Range = High-Low+1 = 100 > 86 (E3); density passes tier 10
; (12*100 = 1200 >= 100*10), so E3 alone refuses.
define void @h4_range_gt_86(i32 noundef %x) {
; CHECK-LABEL: h4_range_gt_86:
; NONEG-NOT: {{\.LJTI}}
; NONEG-NOT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 7, label %c7
    i32 13, label %c13
    i32 21, label %c21
    i32 29, label %c29
    i32 37, label %c37
    i32 45, label %c45
    i32 53, label %c53
    i32 61, label %c61
    i32 73, label %c73
    i32 89, label %c89
    i32 99, label %c99
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c21:
  store volatile i32 21, ptr @glob, align 1
  ret void
c29:
  store volatile i32 29, ptr @glob, align 1
  ret void
c37:
  store volatile i32 37, ptr @glob, align 1
  ret void
c45:
  store volatile i32 45, ptr @glob, align 1
  ret void
c53:
  store volatile i32 53, ptr @glob, align 1
  ret void
c61:
  store volatile i32 61, ptr @glob, align 1
  ret void
c73:
  store volatile i32 73, ptr @glob, align 1
  ret void
c89:
  store volatile i32 89, ptr @glob, align 1
  ret void
c99:
  store volatile i32 99, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; H4-3: density below the tier-10 threshold (E5): N=4 over Range=50,
; 4*100 = 400 < 50*10 = 500. Entries 50 <= 86, so E5 alone refuses.
define void @h4_low_density(i32 noundef %x) {
; CHECK-LABEL: h4_low_density:
; NONEG-NOT: {{\.LJTI}}
; NONEG-NOT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 10, label %c10
    i32 20, label %c20
    i32 45, label %c45
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c20:
  store volatile i32 20, ptr @glob, align 1
  ret void
c45:
  store volatile i32 45, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; H4-5 (rev3): optsize + Range=100 > 86, density passes the OPTSIZE tier
; (80*100 = 8000 >= 100*40 = 4000). The base-class check would
; short-circuit its cap and build the table; backend E3 refuses -- chain.
define void @h4_e3_optsize_no_bypass(i32 noundef %x) nounwind optsize {
; CHECK-LABEL: h4_e3_optsize_no_bypass:
; NONEG-NOT: {{\.LJTI}}
; NONEG-NOT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
    i32 16, label %c16
    i32 17, label %c17
    i32 18, label %c18
    i32 19, label %c19
    i32 20, label %c20
    i32 21, label %c21
    i32 22, label %c22
    i32 23, label %c23
    i32 24, label %c24
    i32 25, label %c25
    i32 26, label %c26
    i32 27, label %c27
    i32 28, label %c28
    i32 29, label %c29
    i32 30, label %c30
    i32 31, label %c31
    i32 32, label %c32
    i32 33, label %c33
    i32 34, label %c34
    i32 35, label %c35
    i32 36, label %c36
    i32 37, label %c37
    i32 38, label %c38
    i32 39, label %c39
    i32 60, label %c60
    i32 61, label %c61
    i32 62, label %c62
    i32 63, label %c63
    i32 64, label %c64
    i32 65, label %c65
    i32 66, label %c66
    i32 67, label %c67
    i32 68, label %c68
    i32 69, label %c69
    i32 70, label %c70
    i32 71, label %c71
    i32 72, label %c72
    i32 73, label %c73
    i32 74, label %c74
    i32 75, label %c75
    i32 76, label %c76
    i32 77, label %c77
    i32 78, label %c78
    i32 79, label %c79
    i32 80, label %c80
    i32 81, label %c81
    i32 82, label %c82
    i32 83, label %c83
    i32 84, label %c84
    i32 85, label %c85
    i32 86, label %c86
    i32 87, label %c87
    i32 88, label %c88
    i32 89, label %c89
    i32 90, label %c90
    i32 91, label %c91
    i32 92, label %c92
    i32 93, label %c93
    i32 94, label %c94
    i32 95, label %c95
    i32 96, label %c96
    i32 97, label %c97
    i32 98, label %c98
    i32 99, label %c99
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
c16:
  store volatile i32 16, ptr @glob, align 1
  ret void
c17:
  store volatile i32 17, ptr @glob, align 1
  ret void
c18:
  store volatile i32 18, ptr @glob, align 1
  ret void
c19:
  store volatile i32 19, ptr @glob, align 1
  ret void
c20:
  store volatile i32 20, ptr @glob, align 1
  ret void
c21:
  store volatile i32 21, ptr @glob, align 1
  ret void
c22:
  store volatile i32 22, ptr @glob, align 1
  ret void
c23:
  store volatile i32 23, ptr @glob, align 1
  ret void
c24:
  store volatile i32 24, ptr @glob, align 1
  ret void
c25:
  store volatile i32 25, ptr @glob, align 1
  ret void
c26:
  store volatile i32 26, ptr @glob, align 1
  ret void
c27:
  store volatile i32 27, ptr @glob, align 1
  ret void
c28:
  store volatile i32 28, ptr @glob, align 1
  ret void
c29:
  store volatile i32 29, ptr @glob, align 1
  ret void
c30:
  store volatile i32 30, ptr @glob, align 1
  ret void
c31:
  store volatile i32 31, ptr @glob, align 1
  ret void
c32:
  store volatile i32 32, ptr @glob, align 1
  ret void
c33:
  store volatile i32 33, ptr @glob, align 1
  ret void
c34:
  store volatile i32 34, ptr @glob, align 1
  ret void
c35:
  store volatile i32 35, ptr @glob, align 1
  ret void
c36:
  store volatile i32 36, ptr @glob, align 1
  ret void
c37:
  store volatile i32 37, ptr @glob, align 1
  ret void
c38:
  store volatile i32 38, ptr @glob, align 1
  ret void
c39:
  store volatile i32 39, ptr @glob, align 1
  ret void
c60:
  store volatile i32 60, ptr @glob, align 1
  ret void
c61:
  store volatile i32 61, ptr @glob, align 1
  ret void
c62:
  store volatile i32 62, ptr @glob, align 1
  ret void
c63:
  store volatile i32 63, ptr @glob, align 1
  ret void
c64:
  store volatile i32 64, ptr @glob, align 1
  ret void
c65:
  store volatile i32 65, ptr @glob, align 1
  ret void
c66:
  store volatile i32 66, ptr @glob, align 1
  ret void
c67:
  store volatile i32 67, ptr @glob, align 1
  ret void
c68:
  store volatile i32 68, ptr @glob, align 1
  ret void
c69:
  store volatile i32 69, ptr @glob, align 1
  ret void
c70:
  store volatile i32 70, ptr @glob, align 1
  ret void
c71:
  store volatile i32 71, ptr @glob, align 1
  ret void
c72:
  store volatile i32 72, ptr @glob, align 1
  ret void
c73:
  store volatile i32 73, ptr @glob, align 1
  ret void
c74:
  store volatile i32 74, ptr @glob, align 1
  ret void
c75:
  store volatile i32 75, ptr @glob, align 1
  ret void
c76:
  store volatile i32 76, ptr @glob, align 1
  ret void
c77:
  store volatile i32 77, ptr @glob, align 1
  ret void
c78:
  store volatile i32 78, ptr @glob, align 1
  ret void
c79:
  store volatile i32 79, ptr @glob, align 1
  ret void
c80:
  store volatile i32 80, ptr @glob, align 1
  ret void
c81:
  store volatile i32 81, ptr @glob, align 1
  ret void
c82:
  store volatile i32 82, ptr @glob, align 1
  ret void
c83:
  store volatile i32 83, ptr @glob, align 1
  ret void
c84:
  store volatile i32 84, ptr @glob, align 1
  ret void
c85:
  store volatile i32 85, ptr @glob, align 1
  ret void
c86:
  store volatile i32 86, ptr @glob, align 1
  ret void
c87:
  store volatile i32 87, ptr @glob, align 1
  ret void
c88:
  store volatile i32 88, ptr @glob, align 1
  ret void
c89:
  store volatile i32 89, ptr @glob, align 1
  ret void
c90:
  store volatile i32 90, ptr @glob, align 1
  ret void
c91:
  store volatile i32 91, ptr @glob, align 1
  ret void
c92:
  store volatile i32 92, ptr @glob, align 1
  ret void
c93:
  store volatile i32 93, ptr @glob, align 1
  ret void
c94:
  store volatile i32 94, ptr @glob, align 1
  ret void
c95:
  store volatile i32 95, ptr @glob, align 1
  ret void
c96:
  store volatile i32 96, ptr @glob, align 1
  ret void
c97:
  store volatile i32 97, ptr @glob, align 1
  ret void
c98:
  store volatile i32 98, ptr @glob, align 1
  ret void
c99:
  store volatile i32 99, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

; H4-6 (rev3): minsize behaves identically (hasOptSize covers MinSize,
; measured P9-D).
define void @h4_e3_minsize_no_bypass(i32 noundef %x) nounwind minsize {
; CHECK-LABEL: h4_e3_minsize_no_bypass:
; NONEG-NOT: {{\.LJTI}}
; NONEG-NOT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
    i32 16, label %c16
    i32 17, label %c17
    i32 18, label %c18
    i32 19, label %c19
    i32 20, label %c20
    i32 21, label %c21
    i32 22, label %c22
    i32 23, label %c23
    i32 24, label %c24
    i32 25, label %c25
    i32 26, label %c26
    i32 27, label %c27
    i32 28, label %c28
    i32 29, label %c29
    i32 30, label %c30
    i32 31, label %c31
    i32 32, label %c32
    i32 33, label %c33
    i32 34, label %c34
    i32 35, label %c35
    i32 36, label %c36
    i32 37, label %c37
    i32 38, label %c38
    i32 39, label %c39
    i32 60, label %c60
    i32 61, label %c61
    i32 62, label %c62
    i32 63, label %c63
    i32 64, label %c64
    i32 65, label %c65
    i32 66, label %c66
    i32 67, label %c67
    i32 68, label %c68
    i32 69, label %c69
    i32 70, label %c70
    i32 71, label %c71
    i32 72, label %c72
    i32 73, label %c73
    i32 74, label %c74
    i32 75, label %c75
    i32 76, label %c76
    i32 77, label %c77
    i32 78, label %c78
    i32 79, label %c79
    i32 80, label %c80
    i32 81, label %c81
    i32 82, label %c82
    i32 83, label %c83
    i32 84, label %c84
    i32 85, label %c85
    i32 86, label %c86
    i32 87, label %c87
    i32 88, label %c88
    i32 89, label %c89
    i32 90, label %c90
    i32 91, label %c91
    i32 92, label %c92
    i32 93, label %c93
    i32 94, label %c94
    i32 95, label %c95
    i32 96, label %c96
    i32 97, label %c97
    i32 98, label %c98
    i32 99, label %c99
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
c16:
  store volatile i32 16, ptr @glob, align 1
  ret void
c17:
  store volatile i32 17, ptr @glob, align 1
  ret void
c18:
  store volatile i32 18, ptr @glob, align 1
  ret void
c19:
  store volatile i32 19, ptr @glob, align 1
  ret void
c20:
  store volatile i32 20, ptr @glob, align 1
  ret void
c21:
  store volatile i32 21, ptr @glob, align 1
  ret void
c22:
  store volatile i32 22, ptr @glob, align 1
  ret void
c23:
  store volatile i32 23, ptr @glob, align 1
  ret void
c24:
  store volatile i32 24, ptr @glob, align 1
  ret void
c25:
  store volatile i32 25, ptr @glob, align 1
  ret void
c26:
  store volatile i32 26, ptr @glob, align 1
  ret void
c27:
  store volatile i32 27, ptr @glob, align 1
  ret void
c28:
  store volatile i32 28, ptr @glob, align 1
  ret void
c29:
  store volatile i32 29, ptr @glob, align 1
  ret void
c30:
  store volatile i32 30, ptr @glob, align 1
  ret void
c31:
  store volatile i32 31, ptr @glob, align 1
  ret void
c32:
  store volatile i32 32, ptr @glob, align 1
  ret void
c33:
  store volatile i32 33, ptr @glob, align 1
  ret void
c34:
  store volatile i32 34, ptr @glob, align 1
  ret void
c35:
  store volatile i32 35, ptr @glob, align 1
  ret void
c36:
  store volatile i32 36, ptr @glob, align 1
  ret void
c37:
  store volatile i32 37, ptr @glob, align 1
  ret void
c38:
  store volatile i32 38, ptr @glob, align 1
  ret void
c39:
  store volatile i32 39, ptr @glob, align 1
  ret void
c60:
  store volatile i32 60, ptr @glob, align 1
  ret void
c61:
  store volatile i32 61, ptr @glob, align 1
  ret void
c62:
  store volatile i32 62, ptr @glob, align 1
  ret void
c63:
  store volatile i32 63, ptr @glob, align 1
  ret void
c64:
  store volatile i32 64, ptr @glob, align 1
  ret void
c65:
  store volatile i32 65, ptr @glob, align 1
  ret void
c66:
  store volatile i32 66, ptr @glob, align 1
  ret void
c67:
  store volatile i32 67, ptr @glob, align 1
  ret void
c68:
  store volatile i32 68, ptr @glob, align 1
  ret void
c69:
  store volatile i32 69, ptr @glob, align 1
  ret void
c70:
  store volatile i32 70, ptr @glob, align 1
  ret void
c71:
  store volatile i32 71, ptr @glob, align 1
  ret void
c72:
  store volatile i32 72, ptr @glob, align 1
  ret void
c73:
  store volatile i32 73, ptr @glob, align 1
  ret void
c74:
  store volatile i32 74, ptr @glob, align 1
  ret void
c75:
  store volatile i32 75, ptr @glob, align 1
  ret void
c76:
  store volatile i32 76, ptr @glob, align 1
  ret void
c77:
  store volatile i32 77, ptr @glob, align 1
  ret void
c78:
  store volatile i32 78, ptr @glob, align 1
  ret void
c79:
  store volatile i32 79, ptr @glob, align 1
  ret void
c80:
  store volatile i32 80, ptr @glob, align 1
  ret void
c81:
  store volatile i32 81, ptr @glob, align 1
  ret void
c82:
  store volatile i32 82, ptr @glob, align 1
  ret void
c83:
  store volatile i32 83, ptr @glob, align 1
  ret void
c84:
  store volatile i32 84, ptr @glob, align 1
  ret void
c85:
  store volatile i32 85, ptr @glob, align 1
  ret void
c86:
  store volatile i32 86, ptr @glob, align 1
  ret void
c87:
  store volatile i32 87, ptr @glob, align 1
  ret void
c88:
  store volatile i32 88, ptr @glob, align 1
  ret void
c89:
  store volatile i32 89, ptr @glob, align 1
  ret void
c90:
  store volatile i32 90, ptr @glob, align 1
  ret void
c91:
  store volatile i32 91, ptr @glob, align 1
  ret void
c92:
  store volatile i32 92, ptr @glob, align 1
  ret void
c93:
  store volatile i32 93, ptr @glob, align 1
  ret void
c94:
  store volatile i32 94, ptr @glob, align 1
  ret void
c95:
  store volatile i32 95, ptr @glob, align 1
  ret void
c96:
  store volatile i32 96, ptr @glob, align 1
  ret void
c97:
  store volatile i32 97, ptr @glob, align 1
  ret void
c98:
  store volatile i32 98, ptr @glob, align 1
  ret void
c99:
  store volatile i32 99, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1, !2, !3, !4, !5, !6}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_h4_few_cases", i32 1, i32 0}
!2 = !{!"_h4_range_gt_86", i32 1, i32 0}
!3 = !{!"_h4_low_density", i32 1, i32 0}
!4 = !{!"_h4_e3_optsize_no_bypass", i32 1, i32 0}
!5 = !{!"_h4_e3_minsize_no_bypass", i32 1, i32 0}
!6 = !{!"_glob", i32 2, i32 0}
;--- i64.ll
; H4-4 (review round 2): i64 conditions.
;
; Two independent layers, honestly labelled:
;  1. ABI layer (pinned below): an i64 scalar ARGUMENT is rejected at the
;     calling-convention boundary before any switch handling. That diagnostic
;     is real but it is NOT a jump-table product and proves nothing about E2.
;  2. E2 layer: registered capability gap, REMOVED from the H4 gate. An
;     internal i64 switch (condition from memory, no argument ABI involved)
;     crashes in DAG type legalization (SIGSEGV via
;     MCS251TargetLowering::LowerOperation <- ExpandIntegerOperand) -- with
;     -mcs251-jump-tables OFF as well, i.e. the comparison-chain fallback for
;     an i64 switch is what is unimplemented, independent of the jump-table
;     work (verified identical on the pre-S1 backend tree, which predates the
;     -mcs251-jump-tables option). llc never reaches a jump-table decision
;     product on that input, so no E2 fallback behaviour can be observed
;     end-to-end. The `not --crash` RUN below pins the crash so the gap stays
;     loud; when wide-integer runtime is connected, replace it with a real
;     E2-fallback pin.
; I64: LLVM ERROR: MCS251: arguments must be unsplit i8/i16/i32 scalars
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

define void @h4_i64(i64 noundef %x) {
entry:
  switch i64 %x, label %default [
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_h4_i64", i32 1, i32 0}
;--- i64int.ll
; H4-4 E2 layer: the INTERNAL i64 switch (condition from memory -- no argument
; ABI in play). capability gap, see the i64.ll header: the comparison-chain
; fallback for an i64 switch crashes in DAG type legalization, with and
; without the jump-table flag (both pinned), so no E2 verdict is observable.
; Pinned to keep the gap loud; replace when wide-integer runtime lands.
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1
@buf = external dso_local global i64, align 1

define void @h4_i64_internal() {
entry:
  %v = load i64, ptr @buf, align 1
  switch i64 %v, label %default [
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_h4_i64_internal", i32 1, i32 0}
;--- i1.ll
; H4-4 adjacent-width guard: a sub-8-bit condition is outside the E2 width set
; {8,16,32} (and, having at most 2 cases, also below the E4 minimum), so no
; table may be built under ANY gate state and the pure comparison chain must
; come out. The chain shape itself (mask + cmp + je) proves no jump-table
; dispatch is present.
; I1: anl r{{[0-9]+}}, #0x01
; I1-NEXT: cmp r{{[0-9]+}}, #0x00
; I1-NEXT: je {{\.LBB[0-9_]+}}
; I1-NOT: jmp @a+dptr
; I1-NOT: LJTI
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1
@buf = external dso_local global i8, align 1

define void @h4_i1_internal() {
entry:
  %b = load i8, ptr @buf, align 1
  %v = trunc i8 %b to i1
  switch i1 %v, label %default [
    i1 0, label %c0
    i1 1, label %c1
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_h4_i1_internal", i32 1, i32 0}
;--- pos

;--- pos.ll
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@glob = external dso_local global i32, align 1

; H4 positive control: optsize + dense Range=40 <= 86, density 100% --
; the table IS built (the override deletes only the size short-circuit,
; not the optsize path).
define void @h4_pos_optsize_dense(i32 noundef %x) nounwind optsize {
; CHECK-LABEL: h4_pos_optsize_dense:
; POS: mov dptr, #.LJTI
; POS-NEXT: jmp @a+dptr
entry:
  switch i32 %x, label %default [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
    i32 5, label %c5
    i32 6, label %c6
    i32 7, label %c7
    i32 8, label %c8
    i32 9, label %c9
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
    i32 16, label %c16
    i32 17, label %c17
    i32 18, label %c18
    i32 19, label %c19
    i32 20, label %c20
    i32 21, label %c21
    i32 22, label %c22
    i32 23, label %c23
    i32 24, label %c24
    i32 25, label %c25
    i32 26, label %c26
    i32 27, label %c27
    i32 28, label %c28
    i32 29, label %c29
    i32 30, label %c30
    i32 31, label %c31
    i32 32, label %c32
    i32 33, label %c33
    i32 34, label %c34
    i32 35, label %c35
    i32 36, label %c36
    i32 37, label %c37
    i32 38, label %c38
    i32 39, label %c39
  ]
c0:
  store volatile i32 0, ptr @glob, align 1
  ret void
c1:
  store volatile i32 1, ptr @glob, align 1
  ret void
c2:
  store volatile i32 2, ptr @glob, align 1
  ret void
c3:
  store volatile i32 3, ptr @glob, align 1
  ret void
c4:
  store volatile i32 4, ptr @glob, align 1
  ret void
c5:
  store volatile i32 5, ptr @glob, align 1
  ret void
c6:
  store volatile i32 6, ptr @glob, align 1
  ret void
c7:
  store volatile i32 7, ptr @glob, align 1
  ret void
c8:
  store volatile i32 8, ptr @glob, align 1
  ret void
c9:
  store volatile i32 9, ptr @glob, align 1
  ret void
c10:
  store volatile i32 10, ptr @glob, align 1
  ret void
c11:
  store volatile i32 11, ptr @glob, align 1
  ret void
c12:
  store volatile i32 12, ptr @glob, align 1
  ret void
c13:
  store volatile i32 13, ptr @glob, align 1
  ret void
c14:
  store volatile i32 14, ptr @glob, align 1
  ret void
c15:
  store volatile i32 15, ptr @glob, align 1
  ret void
c16:
  store volatile i32 16, ptr @glob, align 1
  ret void
c17:
  store volatile i32 17, ptr @glob, align 1
  ret void
c18:
  store volatile i32 18, ptr @glob, align 1
  ret void
c19:
  store volatile i32 19, ptr @glob, align 1
  ret void
c20:
  store volatile i32 20, ptr @glob, align 1
  ret void
c21:
  store volatile i32 21, ptr @glob, align 1
  ret void
c22:
  store volatile i32 22, ptr @glob, align 1
  ret void
c23:
  store volatile i32 23, ptr @glob, align 1
  ret void
c24:
  store volatile i32 24, ptr @glob, align 1
  ret void
c25:
  store volatile i32 25, ptr @glob, align 1
  ret void
c26:
  store volatile i32 26, ptr @glob, align 1
  ret void
c27:
  store volatile i32 27, ptr @glob, align 1
  ret void
c28:
  store volatile i32 28, ptr @glob, align 1
  ret void
c29:
  store volatile i32 29, ptr @glob, align 1
  ret void
c30:
  store volatile i32 30, ptr @glob, align 1
  ret void
c31:
  store volatile i32 31, ptr @glob, align 1
  ret void
c32:
  store volatile i32 32, ptr @glob, align 1
  ret void
c33:
  store volatile i32 33, ptr @glob, align 1
  ret void
c34:
  store volatile i32 34, ptr @glob, align 1
  ret void
c35:
  store volatile i32 35, ptr @glob, align 1
  ret void
c36:
  store volatile i32 36, ptr @glob, align 1
  ret void
c37:
  store volatile i32 37, ptr @glob, align 1
  ret void
c38:
  store volatile i32 38, ptr @glob, align 1
  ret void
c39:
  store volatile i32 39, ptr @glob, align 1
  ret void
default:
  store volatile i32 999, ptr @glob, align 1
  ret void
}

!llvm.module.flags = !{!0}
!mcs251.signatures = !{!1, !2}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{!"_h4_pos_optsize_dense", i32 1, i32 0}
!2 = !{!"_glob", i32 2, i32 0}
