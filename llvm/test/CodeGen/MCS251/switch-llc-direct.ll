; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 %t/u8.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -O2 %t/u8.ll -o - | FileCheck %s --check-prefix=U8
; RUN: llc -mtriple=mcs251 -O0 %t/i16.ll -o /dev/null
; RUN: llc -mtriple=mcs251 -O2 %t/i16.ll -o - | FileCheck %s --check-prefix=I16

; BRJT S1 (design S3.1.3 G2, second source): llc reading clang-built -O0 IR
; of the two original P1 crash shapes -- the demo43 LCD_direction switch
; (u8 promoted to i32, 4 dense cases, side-effect bodies) and a 16-case dense
; i16 switch. Before S1 both crashed with "Cannot select: br_jt" at -O0 AND
; -O2; now they compile through the comparison chain at both levels.
; The IR is the verbatim clang output of the P1 probes (only the split-file
; wrapper is new), so the "second source" channel is pinned end to end.
;
\
; The -O2 chains keep one compare per case value:
; U8-COUNT-4: cmp
; I16-COUNT-16: cmp
;--- u8.ll
; ModuleID = 'switch4-u8.c'
source_filename = "switch4-u8.c"
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

@width = external dso_local global i32, align 1
@height = external dso_local global i32, align 1

; Function Attrs: noinline nounwind optnone
define dso_local void @LCD_direction(i8 noundef zeroext %0) addrspace(4) #0 {
  %2 = alloca i8, align 1
  store i8 %0, ptr %2, align 1
  %3 = load i8, ptr %2, align 1
  %4 = zext i8 %3 to i32
  switch i32 %4, label %9 [
    i32 0, label %5
    i32 1, label %6
    i32 2, label %7
    i32 3, label %8
  ]

5:                                                ; preds = %1
  store volatile i32 128, ptr @width, align 1
  store volatile i32 160, ptr @height, align 1
  call addrspace(4) void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext 0) #2
  br label %10

6:                                                ; preds = %1
  store volatile i32 160, ptr @width, align 1
  store volatile i32 128, ptr @height, align 1
  call addrspace(4) void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext 96) #2
  br label %10

7:                                                ; preds = %1
  store volatile i32 128, ptr @width, align 1
  store volatile i32 160, ptr @height, align 1
  call addrspace(4) void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext -48) #2
  br label %10

8:                                                ; preds = %1
  store volatile i32 160, ptr @width, align 1
  store volatile i32 128, ptr @height, align 1
  call addrspace(4) void @LCD_WriteReg(i8 noundef zeroext 54, i8 noundef zeroext -80) #2
  br label %10

9:                                                ; preds = %1
  br label %10

10:                                               ; preds = %9, %8, %7, %6, %5
  ret void
}

declare dso_local void @LCD_WriteReg(i8 noundef zeroext, i8 noundef zeroext) addrspace(4) #1

attributes #0 = { noinline nounwind optnone "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!mcs251.signatures = !{!3, !4}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 24.0.0git (https://github.com/PhyEx-ux/llvm-project.git f3ac7c692158b6d2c3e0db8207b765e1d0c1367e)"}
!3 = !{!"_LCD_WriteReg", i32 2, i32 0, i32 0, i32 0}
!4 = !{!"_LCD_direction", i32 1, i32 0, i32 0}
;--- i16.ll
; ModuleID = 'switch16-i16.c'
source_filename = "switch16-i16.c"
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

; Function Attrs: noinline nounwind optnone
define dso_local void @f(i32 noundef %0) addrspace(4) #0 {
  %2 = alloca i32, align 1
  store i32 %0, ptr %2, align 1
  %3 = load i32, ptr %2, align 1
  switch i32 %3, label %20 [
    i32 0, label %4
    i32 1, label %5
    i32 2, label %6
    i32 3, label %7
    i32 4, label %8
    i32 5, label %9
    i32 6, label %10
    i32 7, label %11
    i32 8, label %12
    i32 9, label %13
    i32 10, label %14
    i32 11, label %15
    i32 12, label %16
    i32 13, label %17
    i32 14, label %18
    i32 15, label %19
  ]

4:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 100) #2
  br label %21

5:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 101) #2
  br label %21

6:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 102) #2
  br label %21

7:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 103) #2
  br label %21

8:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 104) #2
  br label %21

9:                                                ; preds = %1
  call addrspace(4) void @sink(i32 noundef 105) #2
  br label %21

10:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 106) #2
  br label %21

11:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 107) #2
  br label %21

12:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 108) #2
  br label %21

13:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 109) #2
  br label %21

14:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 110) #2
  br label %21

15:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 111) #2
  br label %21

16:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 112) #2
  br label %21

17:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 113) #2
  br label %21

18:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 114) #2
  br label %21

19:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 115) #2
  br label %21

20:                                               ; preds = %1
  call addrspace(4) void @sink(i32 noundef 0) #2
  br label %21

21:                                               ; preds = %20, %19, %18, %17, %16, %15, %14, %13, %12, %11, %10, %9, %8, %7, %6, %5, %4
  ret void
}

declare dso_local void @sink(i32 noundef) addrspace(4) #1

attributes #0 = { noinline nounwind optnone "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!mcs251.signatures = !{!3, !4}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 24.0.0git (https://github.com/PhyEx-ux/llvm-project.git f3ac7c692158b6d2c3e0db8207b765e1d0c1367e)"}
!3 = !{!"_f", i32 1, i32 0, i32 0}
!4 = !{!"_sink", i32 2, i32 0, i32 0}
