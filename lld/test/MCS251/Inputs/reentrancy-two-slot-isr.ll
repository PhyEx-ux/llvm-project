; E1/WP2 R11-1 regression fixture: two registered ISRs, each calling its own
; two-argument leaf.  The two leaves' static parameter slots live in separate
; overlaid OSEG sections and therefore land on the same final address range.
; The preemption pair is supplied by the test's config file, so the
; ISR-vs-ISR combination is a PROVEN violation (default rc=1).
target triple = "mcs251-unknown-none"
@result = global i16 0, align 1
@llvm.used = appending global [2 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq1 to ptr), ptr addrspacecast (ptr addrspace(4) @irq2 to ptr)], section "llvm.metadata"
define mcs251_intrcc void @irq1() addrspace(4) #0 {
 call addrspace(4) void @left(i16 11, i16 12)
 ret void
}
define mcs251_intrcc void @irq2() addrspace(4) #1 {
 call addrspace(4) void @right(i16 21, i16 22)
 ret void
}
define internal void @left(i16 %a, i16 %b) addrspace(4) #2 {
 %sum = add i16 %a, %b
 store volatile i16 %sum, ptr @result
 ret void
}
define internal void @right(i16 %a, i16 %b) addrspace(4) #2 {
 %sum = xor i16 %a, %b
 store volatile i16 %sum, ptr @result
 ret void
}
define i16 @main() addrspace(4) #2 {
 ret i16 0
}
attributes #0 = { noinline optnone nounwind "mcs251-isr-vector"="1" }
attributes #1 = { noinline optnone nounwind "mcs251-isr-vector"="2" }
attributes #2 = { noinline optnone nounwind }
!mcs251.signatures = !{!0,!1,!2}
!0 = !{!"_irq1",i32 1,i32 0}
!1 = !{!"_irq2",i32 1,i32 0}
!2 = !{!"_main",i32 1,i32 0}
