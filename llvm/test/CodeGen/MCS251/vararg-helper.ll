; G2 B-S2 va_list forwarding golden (G2-VARIADIC-DESIGN-draft.md R3
; §4.3.3(b)): the va_list pair carries the OWNER's slot-area identity in
; its __base VALUE.  An ordinary helper function receives `va_list ap` as a
; plain pointer and its va_arg chain (the exact IR clang's
; MCS251ABIInfo::EmitVAArg emits inside the helper) reads ONLY through that
; pair: the helper never references a _PARM symbol of its own.  This is
; the R3 fix for the "slot-number-only va_list" defect (a pure slot index
; would make a helper read its own _helper_PARM_n slots instead).

; RUN: llc -mtriple=mcs251 < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj < %s | llvm-readelf -sW - | FileCheck %s --check-prefix=OBJ

%struct.__va_list_tag = type { ptr, i32 }

define i32 @own(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %r = call i32 @consume2(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %r
}

define i32 @consume2(ptr %ap) addrspace(4) {
entry:
  %offp1 = getelementptr inbounds i8, ptr %ap, i32 4
  %off1 = load i32, ptr %offp1, align 1
  %ok1 = icmp ule i32 %off1, 20
  br i1 %ok1, label %l1, label %h1
h1:
  call void @llvm.mcs251.vararg.halt()
  unreachable
l1:
  %base1 = load ptr, ptr %ap, align 1
  %addr1 = getelementptr inbounds i8, ptr %base1, i32 %off1
  %v1 = load i32, ptr %addr1, align 1
  %offn1 = add i32 %off1, 4
  store i32 %offn1, ptr %offp1, align 1
  %ok2 = icmp ule i32 %offn1, 20
  br i1 %ok2, label %l2, label %h2
h2:
  call void @llvm.mcs251.vararg.halt()
  unreachable
l2:
  %base2 = load ptr, ptr %ap, align 1
  %addr2 = getelementptr inbounds i8, ptr %base2, i32 %offn1
  %v2 = load i32, ptr %addr2, align 1
  %offn2 = add i32 %offn1, 4
  store i32 %offn2, ptr %offp1, align 1
  %s = add i32 %v1, %v2
  ret i32 %s
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare void @llvm.mcs251.vararg.halt()

!mcs251.signatures = !{!0, !1}
!0 = !{!"_own", i32 9, i32 0, i32 0}
!1 = !{!"_consume2", i32 9, i32 0, i32 0}

; The owner is the only function with a variadic slot area; va_start
; anchors it on _own_PARM_2 (F=1).
; CHECK: .globl _own_PARM_2
; CHECK: _own_PARM_2:
; CHECK: _own:
; CHECK: .db 0x7e, 0x08, (_own_PARM_2) >> 8, (_own_PARM_2)
; CHECK: .db 0x7a, 0x0c, 0x00, (_own_PARM_2) >> 16

; The helper forwards the pair (a pointer argument), consumes two slots,
; and contains exactly the two halt arms of its va_arg chains.
; CHECK: _consume2:
; CHECK: mov dr12, #0x0015
; CHECK: ; %h1
; CHECK: sjmp .
; CHECK: ; %l1
; CHECK: mov dr12, #0x0015
; CHECK: ; %h2
; CHECK: sjmp .

; THE identity property: no _PARM symbol of the helper exists anywhere --
; the helper's slot reads go exclusively through the forwarded __base.
; CHECK-NOT: _consume2_PARM

; Object: only the owner's continuation slots are defined.
; OBJ: _own_PARM_2
; OBJ-NOT: OBJECT GLOBAL DEFAULT {{[0-9]+}} _consume2
