; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,1,1 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,O0
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,2,16,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=CHECK,O2
;
; Near AS0 values use only DPL/DPH. Far AS3 values remain four-byte canonical
; pointers. Later arguments use named big-endian slots sized from their complete
; pointer type, while direct function calls remain 24-bit CODE calls.

define ptr @near_bump(ptr %p) {
; CHECK-LABEL: _near_bump:
; CHECK: mov {{r[0-9]+}}, dpl
; CHECK: mov {{r[0-9]+}}, dph
; CHECK-NOT: mov {{r[0-9]+}}, b
; CHECK-NOT: mov {{r[0-9]+}}, a
; CHECK: add {{wr[0-9]+}}, #0x0001
; CHECK: mov dpl,
; CHECK: mov dph,
; CHECK-NOT: mov b,
; CHECK-NOT: mov a,
; CHECK: eret
  %q = getelementptr i8, ptr %p, i16 1
  ret ptr %q
}

define ptr addrspace(3) @far_identity(ptr addrspace(3) %p) {
; CHECK-LABEL: _far_identity:
; CHECK-DAG: mov {{r[0-9]+}}, dpl
; CHECK-DAG: mov {{r[0-9]+}}, dph
; CHECK-DAG: mov {{r[0-9]+}}, b
; CHECK: mov b,
; CHECK: mov a,
; CHECK: eret
  ret ptr addrspace(3) %p
}

define i8 @near_second(i8 %tag, ptr %p) {
; CHECK: .globl _near_second_PARM_2
; CHECK: _near_second_PARM_2:
; CHECK-NEXT: .ds 2
; CHECK-LABEL: _near_second:
; CHECK: mov [[NEARSLOT:wr[0-9]+]], #_near_second_PARM_2
; CHECK: mov {{r[0-9]+}}, @[[NEARSLOT]]
; CHECK: add [[NEARSLOT]], #0x0001
; CHECK: mov {{r[0-9]+}}, @[[NEARSLOT]]
  %v = load volatile i8, ptr %p, align 1
  %r = xor i8 %v, %tag
  ret i8 %r
}

define i8 @far_second(i8 %tag, ptr addrspace(3) %p) {
; CHECK: .globl _far_second_PARM_2
; CHECK: _far_second_PARM_2:
; CHECK-NEXT: .ds 4
; CHECK-LABEL: _far_second:
; CHECK: mov {{wr[0-9]+}}, #_far_second_PARM_2
; CHECK: add {{wr[0-9]+}}, #0x0001
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
; CHECK: add {{wr[0-9]+}}, #0x0003
; CHECK: mov {{r[0-9]+}}, @{{wr[0-9]+}}
; CHECK: mov {{r[0-9]+}}, @{{dr[0-9]+}}
  %v = load volatile i8, ptr addrspace(3) %p, align 1
  %r = xor i8 %v, %tag
  ret i8 %r
}

declare i8 @take_near(i8, ptr)
declare i8 @take_far(i8, ptr addrspace(3))

define i8 @call_near() {
; CHECK-LABEL: _call_near:
; CHECK: mov [[CALLNEAR:wr[0-9]+]], #_take_near_PARM_2
; CHECK: mov @[[CALLNEAR]],
; CHECK: add [[CALLNEAR]], #0x0001
; CHECK: mov @[[CALLNEAR]],
; CHECK: ecall _take_near
  %r = call i8 @take_near(i8 7, ptr inttoptr (i16 512 to ptr))
  ret i8 %r
}

define i8 @call_far() {
; CHECK-LABEL: _call_far:
; CHECK: mov [[CALLFAR:wr[0-9]+]], #_take_far_PARM_2
; CHECK: mov @[[CALLFAR]],
; CHECK: add {{wr[0-9]+}}, #0x0003
; CHECK: mov @{{wr[0-9]+}},
; CHECK: ecall _take_far
  %r = call i8 @take_far(i8 9,
      ptr addrspace(3) inttoptr (i32 66048 to ptr addrspace(3)))
  ret i8 %r
}

define void @call_code_pointer(ptr addrspace(4) %fn) addrspace(4) {
; CHECK-LABEL: _call_code_pointer:
; CHECK-DAG: mov {{r[0-9]+}}, dpl
; CHECK-DAG: mov {{r[0-9]+}}, dph
; CHECK-DAG: mov {{r[0-9]+}}, b
; CHECK: ecall @dr{{[0-9]+}}
; CHECK: eret
  call addrspace(4) void %fn()
  ret void
}

define i8 @far_slot_top_byte(i8 %tag, ptr addrspace(3) %p) {
; CHECK-LABEL: _far_slot_top_byte:
; CHECK: mov {{r[0-9]+}}, #0x00
; CHECK: mov dpl,
  %bits = ptrtoint ptr addrspace(3) %p to i32
  %top = lshr i32 %bits, 24
  %byte = trunc i32 %top to i8
  ret i8 %byte
}

define i8 @call_far_polluted() {
; CHECK-LABEL: _call_far_polluted:
; O0: mov wr{{[0-9]+}}, #0xaa01
; O0: mov {{r[0-9]+}}, #0x00
; O2: mov [[ZERO:r[0-9]+]], #0x00
; CHECK: mov [[POLLUTEDSLOT:wr[0-9]+]], #_far_slot_top_byte_PARM_2
; O2: mov @[[POLLUTEDSLOT]], [[ZERO]]
; CHECK: add {{wr[0-9]+}}, #0x0001
; CHECK: mov @{{wr[0-9]+}},
; CHECK: add {{wr[0-9]+}}, #0x0002
; CHECK: mov @{{wr[0-9]+}},
; CHECK: add {{wr[0-9]+}}, #0x0003
; CHECK: mov @{{wr[0-9]+}},
; CHECK: ecall _far_slot_top_byte
  %r = call i8 @far_slot_top_byte(i8 0,
      ptr addrspace(3) inttoptr (i32 2852192771 to ptr addrspace(3)))
  ret i8 %r
}
