; Round-6 review item (7): OBJECT-ORDER regression, input object B.  Two
; registered ISRs, each calling its OWN two-argument leaf, so each leaf's
; static parameter slot is a separate OSEG area the linker overlays on one
; final address.  Linked with `reentrancy-order-a.ll` (which contributes two
; more such ISRs) the same-slot group has SIX ordered (writer root, user
; root) candidates, and the contract used by the regression declares the
; LAST ones -- so several candidates that do NOT establish the group precede
; the ones that do.  `_main` is defined there (the signature protocol wants the
; ordinary entry defined by exactly one input object), so this object
; carries only its own two ISRs and their leaves.
; ModuleID = '-'
source_filename = "-"
target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251-unknown-none"

@acc3 = internal global i16 0, align 1
@acc4 = internal global i16 0, align 1
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq3 to ptr), ptr addrspacecast (ptr addrspace(4) @irq4 to ptr)], section "llvm.metadata"

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @irq3() addrspace(4) #0 {
  call addrspace(4) void @leaf3(i16 noundef zeroext 7, i16 noundef zeroext 11) #5
  ret void
}

; Function Attrs: noinline nounwind optnone
define internal void @leaf3(i16 noundef zeroext %0, i16 noundef zeroext %1) addrspace(4) #1 {
  %3 = add i16 %0, %1
  store volatile i16 %3, ptr @acc3, align 1
  ret void
}

; Function Attrs: noinline nounwind optnone
define dso_local mcs251_intrcc void @irq4() addrspace(4) #2 {
  call addrspace(4) void @leaf4(i16 noundef zeroext 7, i16 noundef zeroext 11) #5
  ret void
}

; Function Attrs: noinline nounwind optnone
define internal void @leaf4(i16 noundef zeroext %0, i16 noundef zeroext %1) addrspace(4) #1 {
  %3 = add i16 %0, %1
  store volatile i16 %3, ptr @acc4, align 1
  ret void
}

attributes #0 = { noinline nounwind optnone "mcs251-isr-vector"="3" }
attributes #1 = { noinline nounwind optnone }
attributes #2 = { noinline nounwind optnone "mcs251-isr-vector"="4" }
attributes #5 = { nobuiltin "no-builtins" }

!mcs251.signatures = !{!0, !1}
!0 = !{!"_irq3", i32 1, i32 0}
!1 = !{!"_irq4", i32 1, i32 0}
