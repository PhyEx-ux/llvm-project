; ModuleID = '/tmp/reen-check/case.c'
source_filename = "/tmp/reen-check/case.c"
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

@isr_acc = internal global i16 0, align 1
@fg_acc = internal global i16 0, align 1
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @timer0_isr to ptr)], section "llvm.metadata"

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @timer0_isr() addrspace(4) #0 {
  call addrspace(4) void @isr_scale(i16 noundef zeroext 3) #2
  ret void
}

; Function Attrs: noinline nounwind optnone
define internal void @isr_scale(i16 noundef zeroext %0) addrspace(4) #1 {
  %2 = alloca i16, align 1
  store i16 %0, ptr %2, align 1
  %3 = load i16, ptr %2, align 1
  %4 = zext i16 %3 to i32
  %5 = mul nsw i32 %4, %4
  %6 = trunc i32 %5 to i16
  store volatile i16 %6, ptr @isr_acc, align 1
  ret void
}

; Function Attrs: noinline nounwind optnone
define dso_local i32 @main() addrspace(4) #1 {
  call addrspace(4) void @isr_scale(i16 noundef zeroext 5) #2
  br label %1

1:                                                ; preds = %1, %0
  br label %1
}

; Function Attrs: noinline nounwind optnone

attributes #0 = { noinline nounwind optnone "frame-pointer"="all" "mcs251-isr-vector"="1" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { noinline nounwind optnone "frame-pointer"="all" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!mcs251.signatures = !{!3, !4}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 24.0.0git (https://github.com/PhyEx-ux/llvm-project.git 39cc15a06b5adbff4b6bf11127c1f54e7dc8b18f)"}
!3 = !{!"_main", i32 1, i32 0}
!4 = !{!"_timer0_isr", i32 1, i32 0}
