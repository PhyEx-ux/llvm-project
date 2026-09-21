; Round-5 review finding 2 fixture: THREE registered ISRs call ONE
; two-argument function, so its single static parameter slot is written by
; three distinct ISR roots and read by the callee under all three roots.
; The same-slot ISR/ISR shape then has SIX ordered (writer root, user
; root) combinations; the per-pair diagnostics must name each line's write
; root and use root, and the A/B side lines must carry the DECIDING pair's
; roots -- never the union over every established pair.
; ModuleID = '-'
source_filename = "-"
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

@v = internal global i16 0, align 1
@llvm.used = appending global [3 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq1 to ptr), ptr addrspacecast (ptr addrspace(4) @irq2 to ptr), ptr addrspacecast (ptr addrspace(4) @irq3 to ptr)], section "llvm.metadata"

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @irq1() addrspace(4) #0 {
  call addrspace(4) void @shared(i16 noundef zeroext 3, i16 noundef zeroext 4) #5
  ret void
}

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @irq2() addrspace(4) #2 {
  call addrspace(4) void @shared(i16 noundef zeroext 3, i16 noundef zeroext 4) #5
  ret void
}

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @irq3() addrspace(4) #3 {
  call addrspace(4) void @shared(i16 noundef zeroext 3, i16 noundef zeroext 4) #5
  ret void
}

; Function Attrs: noinline nounwind optnone
define internal void @shared(i16 noundef zeroext %0, i16 noundef zeroext %1) addrspace(4) #1 {
  %3 = alloca i16, align 1
  %4 = alloca i16, align 1
  store i16 %0, ptr %3, align 1
  store i16 %1, ptr %4, align 1
  %5 = load i16, ptr %3, align 1
  %6 = zext i16 %5 to i32
  %7 = load i16, ptr %4, align 1
  %8 = zext i16 %7 to i32
  %9 = mul nsw i32 %6, %8
  %10 = trunc i32 %9 to i16
  store volatile i16 %10, ptr @v, align 1
  ret void
}

; Function Attrs: noinline nounwind optnone
define dso_local i32 @main() addrspace(4) #1 {
  br label %1

1:                                                ; preds = %1, %0
  br label %1
}

attributes #0 = { noinline nounwind optnone "frame-pointer"="all" "mcs251-isr-vector"="1" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { noinline nounwind optnone "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { noinline nounwind optnone "frame-pointer"="all" "mcs251-isr-vector"="2" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { noinline nounwind optnone "frame-pointer"="all" "mcs251-isr-vector"="3" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #5 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!mcs251.signatures = !{!3, !4, !5, !6}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 24.0.0git (https://github.com/PhyEx-ux/llvm-project.git 39cc15a06b5adbff4b6bf11127c1f54e7dc8b18f)"}
!3 = !{!"_irq1", i32 1, i32 0}
!4 = !{!"_irq2", i32 1, i32 0}
!5 = !{!"_irq3", i32 1, i32 0}
!6 = !{!"_main", i32 1, i32 0}
