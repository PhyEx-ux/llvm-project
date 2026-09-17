; G11-N2 blocking fix (2026-09-17): a fixed AS0-DATA object whose size does
; not fit in 16 bits used to bypass the size gate the ordinary DSEG path
; applies. The fixed emitter then wrote the object size into the two u16
; fields of the sparse XINIT record, truncating them to 0 while the fixed
; section, the symbol st_size and the NOTE size still reported 65536 -- and
; with a non-zero initializer it still appended the full payload after the
; zeroed length fields. The restored guard rejects the object with the same
; frozen wording as the ordinary path ("mutable global size must fit in 16
; bits") BEFORE any fixed section or initialization record is emitted.
;
; The gate covers both AS0 initialization shapes (zero-image record and
; non-zero payload) and both initialization channels of the fixed path
; (AS0 -> .mcs251.xinit, XDATA -> .mcs251.xdata_init). A >65535 noinit AS0
; object is rejected too: noinit suppresses only the initialization record,
; while the fixed section and the NOTE size stay 16-bit-addressed protocol
; values -- admitting them is a separate design decision (registered
; unsupported this round), not a side effect of this guard.
;
; RUN: split-file %s %t
;
; AS0, zero-initialized (the XINIT record would carry size=0/payload=0).
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/as0-big.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AS0
; AS0->AS3 xdata: the dedicated XDATA record half of the same fixed path.
; XDATA has its own frozen wording; the guard order keeps it.
; AS0 with a non-zero initializer (payload appended after the zeroed length).
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/as0-big-init.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AS0
; noinit: no initialization record at all, still rejected.
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/as0-big-noinit.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AS0
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/xdata-big.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=XDATA
;
; AS0: LLVM ERROR: MCS251: mutable global size must fit in 16 bits
; XDATA: LLVM ERROR: MCS251: fixed __xdata global 'x': object size 65536 does not fit the 16-bit XDATA record limit (65535 bytes; fixed objects are never split)
;
; Boundary: size 65535 still succeeds and keeps the untruncated protocol
; values (sh_size == st_size == 65535, XINIT record header = 6 bytes).
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/boundary.ll -o %t/boundary.o
; RUN: llvm-readobj --sections --symbols %t/boundary.o | FileCheck %s --check-prefix=BOUNDARY
; BOUNDARY: Name: .mcu.fixed.x
; BOUNDARY: Size: 65535
; BOUNDARY: Name: .mcs251.xinit
; BOUNDARY: Size: 6
; BOUNDARY: Name: _x
; BOUNDARY: Size: 65535

;--- as0-big.ll
target triple = "mcs251-unknown-none"
%big = type { i8, [65535 x i8] }
@x = global %big zeroinitializer, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="x" }

;--- as0-big-init.ll
target triple = "mcs251-unknown-none"
%big = type { i8, [65535 x i8] }
@x = global %big { i8 1, [65535 x i8] zeroinitializer }, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="x" }

;--- as0-big-noinit.ll
target triple = "mcs251-unknown-none"
%big = type { i8, [65535 x i8] }
@x = global %big zeroinitializer, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x100,data,object,owned,2" "mcs251-stable-symbol"="x" }

;--- xdata-big.ll
target triple = "mcs251-unknown-none"
%big = type { i8, [65535 x i8] }
@x = addrspace(3) global %big zeroinitializer, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x100,xdata,object,owned,0" "mcs251-stable-symbol"="x" }

;--- boundary.ll
target triple = "mcs251-unknown-none"
%small = type { i8, [65534 x i8] }
@x = global %small zeroinitializer, align 1 #0
!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
attributes #0 = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="x" }