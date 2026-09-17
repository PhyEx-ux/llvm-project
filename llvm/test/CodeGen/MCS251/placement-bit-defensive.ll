; G11-N1 blocking fix (2026-09-17): a P09 bit object is object identity, not
; byte storage. It reaches the backend as a default-address-space i8 global
; carrying the structural attribute "mcs251-bit-object" (MCS251BitObject.h),
; and the emitter diverts it into the `.mcs251.bit` kind-1 record before the
; placement dispatch. A hand-written module that ALSO marks the same global
; with "mcs251-place" used to be accepted in both directions:
;
;   * owned  -- the placement was silently swallowed: the object carried only
;               the dynamic `.mcs251.bit` record, with no `.mcu.fixed.*`
;               section and no `.mcs251.placement` NOTE, so the user's
;               constraint disappeared from a successful object;
;   * bind   -- the bind census emitted a "DATA/object/bind size=1" NOTE,
;               encoding the bit handle (a symbolic identity description) as
;               a one-byte DATA object and confusing the identity domain.
;
; This reader is the receiving face for hand-written IR: the design schema
; has no bit storage class and the A layer never produces this pair (Sema
; rejects it with err_mcs251_placement_bit), so the double marking is
; rejected fail-closed instead of choosing one of the two meanings. The
; emitter dispatch order is deliberately NOT swapped: emitting bit identity
; as a byte object would be the wrong fix.
;
; RUN: split-file %s %t
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/owned.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OWNED
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bind.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BIND
;
; OWNED: LLVM ERROR: MCS251: bit object 'bit' also carries the mcs251-place attribute: a bit entity is object identity without a byte storage class and cannot be placed or bound
; BIND: LLVM ERROR: MCS251: bit object 'y' also carries the mcs251-place attribute: a bit entity is object identity without a byte storage class and cannot be placed or bound
;
; Reverse control 1: the same modules WITHOUT the "mcs251-place" attribute
; keep their frozen bit-record / bind-NOTE behaviour -- the guard is additive
; and never engages a placement-free module.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bit-only.ll -o %t/bit-only.o
; RUN: llvm-readobj --sections --notes %t/bit-only.o | FileCheck %s --check-prefix=BITONLY
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bind-only.ll -o %t/bind-only.o
; RUN: llvm-readobj --sections --notes %t/bind-only.o | FileCheck %s --check-prefix=BINDONLY
;
; BITONLY: Name: .mcs251.bit
; BITONLY-NOT: Name: .mcu.fixed.x
; BINDONLY: Name: .mcs251.placement
; BINDONLY: Size: 0x34
;
; Reverse control 2: the legal Bit+Placement keepalive container uses
; DIFFERENT entities (a pure bit member next to a placed object), so the
; per-member classification still grants the exemption and both an identity
; record and a fixed section are emitted. This is the shape the G11-B
; frozen matrix covers; the N1 rejection must not touch it.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/mixed.ll -o %t/mixed.o
; RUN: llvm-readobj --sections %t/mixed.o | FileCheck %s --check-prefix=MIXED
;
; MIXED: Name: .mcs251.bit
; MIXED: Name: .mcu.fixed.xk

;--- owned.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-bit-object" "mcs251-place"="0x30,data,object,owned,0" "mcs251-stable-symbol"="x" }

;--- bind.ll
target triple = "mcs251-unknown-none"
@y = external global i8, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-bit-object" "mcs251-place"="0x30,data,object,bind,0" "mcs251-stable-symbol"="y" }

;--- bit-only.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-bit-object" }

;--- bind-only.ll
target triple = "mcs251-unknown-none"
@y = external global i8, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x30,data,object,bind,0" "mcs251-stable-symbol"="y" }

;--- mixed.ll
target triple = "mcs251-unknown-none"
@bit = global i8 0, align 1 #0
@xk = addrspace(3) global i16 1, align 1 #1
@llvm.used = appending global [2 x ptr] [ptr @bit, ptr addrspacecast (ptr addrspace(3) @xk to ptr)], section "llvm.metadata"
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-bit-object" }
attributes #1 = { "mcs251-place"="0x10010,xdata,object,owned,0" "mcs251-stable-symbol"="xk" }