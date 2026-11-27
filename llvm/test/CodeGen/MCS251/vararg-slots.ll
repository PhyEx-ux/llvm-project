; Stack-based reentrant variadic ABI golden: no static `_PARM_` / OSEG / DSEG
; parameter slots; `va_start` anchors the 8-byte `{base, offset}` `va_list` pair
; on the caller's incoming stack frame via SPX (`0x81`/`0x85`); callers push
; variadic actuals onto the stack (`inc spx` / `@dr60` / `dec spx`), and
; `llvm.mcs251.vararg.halt` lowers to the two-byte dead loop `sjmp .` (`80 FE`).

; RUN: llc -mtriple=mcs251 < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj < %s | llvm-readelf -sW - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj < %s | llvm-readelf -x .text - | FileCheck %s --check-prefix=HALT

%struct.__va_list_tag = type { ptr, i32 }

@gv = global i32 77

define i32 @sum(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load i32, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %v
}

define i32 @callit() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 1, i32 30, i32 40)
  ret i32 %r
}

define float @consume_f32(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load float, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret float %v
}

define ptr @consume_ptr(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load ptr, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret ptr %v
}

define i8 @consume_i8(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %w = load i32, ptr %addr, align 1
  %v = trunc i32 %w to i8
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i8 %v
}

define i16 @consume_i16(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %w = load i32, ptr %addr, align 1
  %v = trunc i32 %w to i16
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  ret i16 %v
}

define internal i32 @twice(i32 %x) addrspace(4) {
  %r = mul i32 %x, 2
  ret i32 %r
}

define i32 @sum_nonleaf(i32 %n, ...) addrspace(4) {
entry:
  %ap = alloca %struct.__va_list_tag, align 1
  call void @llvm.va_start.p0(ptr %ap)
  %offp = getelementptr inbounds i8, ptr %ap, i32 4
  %off = load i32, ptr %offp, align 1
  %ok = icmp ule i32 %off, 20
  br i1 %ok, label %load, label %halt

halt:
  call void @llvm.mcs251.vararg.halt()
  unreachable

load:
  %base = load ptr, ptr %ap, align 1
  %addr = getelementptr inbounds i8, ptr %base, i32 %off
  %v = load i32, ptr %addr, align 1
  %offn = add i32 %off, 4
  store i32 %offn, ptr %offp, align 1
  call void @llvm.va_end.p0(ptr %ap)
  %r = call i32 @twice(i32 %v)
  ret i32 %r
}

define i32 @call_zero() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5)
  ret i32 %r
}

define i32 @call_one() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5, i32 7)
  ret i32 %r
}

define i32 @call_six() addrspace(4) {
  %r = call i32 (i32, ...) @sum(i32 5, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6)
  ret i32 %r
}

define float @call_f32() addrspace(4) {
  %r = call float (i32, ...) @consume_f32(i32 2, float 1.500000e+00)
  ret float %r
}

define ptr @call_ptr() addrspace(4) {
  %r = call ptr (i32, ...) @consume_ptr(i32 2, ptr @gv)
  ret ptr %r
}

define i8 @call_narrow() addrspace(4) {
  %r = call i8 (i32, ...) @consume_i8(i32 3, i32 90, i32 4386)
  ret i8 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)
declare void @llvm.mcs251.vararg.halt()

!mcs251.signatures = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12}
!0 = !{!"_sum", i32 9, i32 0, i32 0}
!1 = !{!"_callit", i32 1, i32 0, i32 0}
!2 = !{!"_consume_f32", i32 9, i32 0, i32 0}
!3 = !{!"_consume_ptr", i32 9, i32 0, i32 0}
!4 = !{!"_consume_i8", i32 9, i32 0, i32 0}
!5 = !{!"_consume_i16", i32 9, i32 0, i32 0}
!6 = !{!"_sum_nonleaf", i32 9, i32 0, i32 0}
!7 = !{!"_call_zero", i32 1, i32 0, i32 0}
!8 = !{!"_call_one", i32 1, i32 0, i32 0}
!9 = !{!"_call_six", i32 1, i32 0, i32 0}
!10 = !{!"_call_narrow", i32 1, i32 0, i32 0}
!11 = !{!"_call_f32", i32 1, i32 0, i32 0}
!12 = !{!"_call_ptr", i32 1, i32 0, i32 0}

; ASM-NOT: .area OSEG
; ASM-NOT: _PARM_

; ASM-LABEL: _sum:
; ASM: mov r{{[0-9]+}}, 0x81
; ASM: mov r{{[0-9]+}}, 0x85
; ASM: ; %halt
; ASM-NEXT: sjmp .
; ASM: ; %load
; ASM: add dr{{[0-9]+}}, dr{{[0-9]+}}
; ASM: eret

; ASM-LABEL: _callit:
; ASM: inc spx, #0x4
; ASM: inc spx, #0x4
; ASM-DAG: mov r{{[0-9]+}}, #0x1e
; ASM-DAG: mov r{{[0-9]+}}, #0x28
; ASM: ecall _sum
; ASM: dec spx, #0x4
; ASM: dec spx, #0x4
; ASM: eret

; ASM-LABEL: _consume_f32:
; ASM: ; %load
; ASM: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; ASM: eret

; ASM-LABEL: _consume_ptr:
; ASM: ; %load
; ASM: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; ASM: eret

; ASM-LABEL: _consume_i8:
; ASM: ; %load
; ASM: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; ASM: eret

; ASM-LABEL: _consume_i16:
; ASM: ; %load
; ASM: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0002
; ASM: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0003
; ASM: eret

; ASM-LABEL: _sum_nonleaf:
; ASM: mov r{{[0-9]+}}, 0x81
; ASM: mov r{{[0-9]+}}, 0x85
; ASM: ecall _twice
; ASM: eret

; ASM-LABEL: _call_zero:
; ASM-NOT: inc spx
; ASM: ecall _sum
; ASM: eret

; ASM-LABEL: _call_one:
; ASM: inc spx, #0x4
; ASM: mov r{{[0-9]+}}, #0x07
; ASM: ecall _sum
; ASM: dec spx, #0x4
; ASM: eret

; ASM-LABEL: _call_six:
; ASM: inc spx, #0x4
; ASM: mov r{{[0-9]+}}, #0x06
; ASM: ecall _sum
; ASM: dec spx, #0x4
; ASM: eret

; ASM-LABEL: _call_f32:
; ASM: inc spx, #0x4
; ASM-DAG: mov r{{[0-9]+}}, #0x3f
; ASM-DAG: mov r{{[0-9]+}}, #0xc0
; ASM: ecall _consume_f32
; ASM: dec spx, #0x4
; ASM: eret

; ASM-LABEL: _call_ptr:
; ASM: (_gv) >> 8, (_gv)
; ASM: inc spx, #0x4
; ASM: ecall _consume_ptr
; ASM: dec spx, #0x4
; ASM: eret

; ASM-LABEL: _call_narrow:
; ASM: inc spx, #0x4
; ASM-DAG: mov r{{[0-9]+}}, #0x5a
; ASM-DAG: mov r{{[0-9]+}}, #0x11
; ASM-DAG: mov r{{[0-9]+}}, #0x22
; ASM: ecall _consume_i8
; ASM: dec spx, #0x4
; ASM: eret

; OBJ-DAG: FUNC GLOBAL DEFAULT {{[0-9]+}} _sum
; OBJ-DAG: FUNC GLOBAL DEFAULT {{[0-9]+}} _sum_nonleaf
; OBJ-DAG: FUNC GLOBAL DEFAULT {{[0-9]+}} _call_six
; OBJ-NOT: _PARM_

; HALT: 80fe
