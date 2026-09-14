; v2 direct calls use AS4 function symbols but do not serialize AS4 function
; pointers into data/parameter slots, so the established ELF v1 identity stays
; valid while the linker receives full 24-bit call relocations.

target datalayout = "E-m:s-p:32:8:8:32-p1:16:8:8:16-p2:16:8:8:16-p3:32:8:8:32-p4:32:8:8:32-p6:16:8:8:16-p7:32:8:8:32-p8:16:8:8:16-p9:32:8:8:32-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8-P4-A0-G0"
target triple = "mcs251"

declare void @external() addrspace(4)

define void @local() addrspace(4) {
  ret void
}

define void @call_external() addrspace(4) {
  call addrspace(4) void @external()
  ret void
}

define void @call_local() addrspace(4) {
  call addrspace(4) void @local()
  ret void
}

; AS6 lowers to a direct byte access and is representable by the existing ELF
; identity. It must not re-enable arbitrary v2 pointer-payload object output.
define void @write_sbuf() addrspace(4) {
  store volatile i8 86, ptr addrspace(6) inttoptr (i16 153 to ptr addrspace(6)), align 1
  ret void
}

!mcs251.signatures = !{!10000, !10001, !10002, !10003, !10004}
!10000 = !{!"_external", i32 2, i32 0}
!10001 = !{!"_local", i32 1, i32 0}
!10002 = !{!"_call_external", i32 1, i32 0}
!10003 = !{!"_call_local", i32 1, i32 0}
!10004 = !{!"_write_sbuf", i32 1, i32 0}
