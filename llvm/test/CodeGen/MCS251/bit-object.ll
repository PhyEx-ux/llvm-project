; BT12: persistent `bit` object emission. A bit object reaches the backend as an
; i8 GlobalVariable placeholder carrying the structural global attribute
; "mcs251-bit-object" (documented in llvm/BinaryFormat/MCS251Bit.h). The
; AsmPrinter diverts it into a `.mcs251.bit` kind-1 record; it is never emitted
; as a byte object (no DSEG/XINIT/CSEG data).
;
; The record is 8 bytes big-endian (lld/MCS251/BIT-OBJECT-CONTRACT.md §3):
;   [0] version=1, [1] kind=1, [2] init (0|1), [3] caps=1,
;   [4..7] four zero bytes associated by R_MCS251_BIT_REF (type 10) at +4.
; The defining symbol is STT_OBJECT size 1 with st_value = the record offset.

; A definition-only TU: two globals (0 and 1 initializer), one internal
; (file-static) and one extern reference (no record of its own).
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/def.ll -o %t/def.o
; RUN: llvm-readobj --sections --section-data --symbols --relocations %t/def.o | FileCheck %s

; The frozen section shape: non-ALLOC PROGBITS, flags 0, alignment 4, no
; entry size, and one 8-byte record per definition in module order.
;CHECK:      Name: .mcs251.bit
;CHECK:      Type: SHT_PROGBITS
;CHECK-NEXT: Flags [ (0x0)
;CHECK:      AddressAlignment: 4
;CHECK:      EntrySize: 0
;CHECK:      SectionData (
;CHECK-NEXT:   0000: 01010101 00000000 01010001 00000000
;CHECK-NEXT:   0010: 01010101 00000000

; Exactly one R_MCS251_BIT_REF per record, at record + 4, naming the exact
; defining symbol (never a section+addend fold).
;CHECK:      Section ({{[0-9]+}}) .rela.mcs251.bit {
;CHECK-NEXT:   0x4 R_MCS251_BIT_REF _flag 0x0
;CHECK-NEXT:   0xC R_MCS251_BIT_REF _zero 0x0
;CHECK-NEXT:   0x14 R_MCS251_BIT_REF _local 0x0

; kind-1 definition symbols: STT_OBJECT size 1 in .mcs251.bit, st_value = the
; record offset (0x0/0x8/0x10). The extern `_ext` is an undefined STT_OBJECT
; reference with no record.
;CHECK:      Name: _local
;CHECK:      Value: 0x10
;CHECK:      Size: 1
;CHECK:      Binding: Local
;CHECK:      Type: Object
;CHECK:      Section: .mcs251.bit
;CHECK:      Name: _flag
;CHECK:      Value: 0x0
;CHECK:      Size: 1
;CHECK:      Binding: Global
;CHECK:      Type: Object
;CHECK:      Section: .mcs251.bit
;CHECK:      Name: _zero
;CHECK:      Value: 0x8
;CHECK:      Size: 1
;CHECK:      Type: Object
;CHECK:      Section: .mcs251.bit
;CHECK:      Name: _ext
;CHECK:      Value: 0x0
;CHECK:      Size: 0
;CHECK:      Type: Object
;CHECK:      Section: Undefined

; The bit object must not create ordinary storage: no DSEG/XINIT data.
;RUN: llvm-readobj --sections %t/def.o | FileCheck %s --check-prefix=NOSTORE --implicit-check-not=.mcs251.dseg --implicit-check-not=.mcs251.xinit

; Negative: the placeholder must be an i8 global with initializer 0 or 1.
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/bad-init.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BADINIT
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/bad-type.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BADTYPE
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/bad-tls.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=BADPLACE

; An extern-only TU (every marked global is a declaration) emits no
; `.mcs251.bit` at all: the linker rejects an empty record section and a
; cross-TU use is a BITADDR8 reference, not a record.
;RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/extern-only.ll -o %t/extern.o
;RUN: llvm-readobj --sections --symbols %t/extern.o | FileCheck %s --check-prefix=EXTERN --implicit-check-not=.mcs251.bit
;EXTERN:      Name: _ext

; A verified keepalive root (llvm.used of only marked bit objects) is
; registration data, not storage: it is consumed without bytes and the object
; keeps its record across -O2.
;RUN: llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t/keepalive.ll -o %t/keep.o
;RUN: llvm-readobj --relocations %t/keep.o | FileCheck %s --check-prefix=KEEP --implicit-check-not=.mcs251.dseg
;KEEP:      Section ({{[0-9]+}}) .rela.mcs251.bit {
;KEEP-NEXT:   0x4 R_MCS251_BIT_REF _flag 0x0

; A keepalive root that also escapes an ordinary global is not exempted.
;RUN: not llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t/keepalive-mixed.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=KEEPMIX
;KEEPMIX: MCS251: defined global data requires

; Escape through a shared constant aggregate: the SAME uniqued aggregate is
; reachable from the keepalive root AND from an ordinary escape (an aggregate
; return). One path reaching a root must not excuse the whole constant, so both
; the O0 and O2 pipelines, and the target entry with the generic verifier
; disabled, must reject it.
;RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/aggregate-return.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AGGRET
;RUN: not llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t/aggregate-return.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AGGRET
;RUN: not llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/aggregate-return.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AGGRET
;RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/aggregate-export.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=AGGRET
; Use the compatibility layout to reach the bit-handle verifier rather than
; stopping at the earlier v2 object-identity gate for addrspacecast.
;RUN: not llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf %t/cast-shared.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CASTSH
;AGGRET: MCS251 contract violation: MCS251 bit object 'flag': handle must not escape through a constant expression or initializer
;CASTSH: LLVM ERROR: MCS251 contract violation: MCS251 bit object 'flag': handle must not escape through a constant expression or initializer

; A call or operand-bundle use is only legitimate as the argument-0 use of a
; fully validated symbolic bit-intrinsic call (P09 section 1.3 B; the positive
; form is covered by bit-intrinsics-obj.ll). Everything else -- a same-named
; non-intrinsic declaration, a real intrinsic carrying the handle in an
; operand bundle, an ordinary call -- is rejected with the frozen P09
; section 1.4 diagnostic.
;RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/fake-intrinsic.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
;RUN: not llc -mtriple=mcs251 -O2 -filetype=obj -mcs251-object-format=elf %t/fake-intrinsic.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
;RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/bundle-intrinsic.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
;CALL: MCS251 contract violation: MCS251 bit object 'flag': handle must not be used by a non-whitelisted call or operand bundle

; The bit-object protocol is ELF-only: REL objects and asm text reject it.
;RUN: not llc -mtriple=mcs251 -filetype=obj %t/def.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REL
;RUN: not llc -mtriple=mcs251 -filetype=asm %t/def.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=REL
;REL: MCS251 bit object requires ELF object output

; The definition record's association relocation must keep the exact named
; symbol (never a section+addend fold): the .rela.mcs251.bit row names `_flag`.
; (Already asserted by the CHECK block above.)

; Negative: a handle escape (ordinary load) is a contract violation, even at a
; target entry that also runs with the generic verifier disabled.
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/escape-load.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPELOAD
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/escape-gep.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPEGEP
;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf %t/escape-ptrtoint.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPEINIT

;RUN: not llc -mtriple=mcs251 -filetype=obj -mcs251-object-format=elf -disable-verify %t/escape-load.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ESCAPELOAD

;BADINIT:  LLVM ERROR: MCS251 bit object 'flag': initializer must be the constant 0 or 1
;BADTYPE:  LLVM ERROR: MCS251 bit object 'flag': placeholder must be an i8 global
;BADPLACE: LLVM ERROR: MCS251 bit object 'flag': unsupported placement or linkage
;ESCAPELOAD: LLVM ERROR: MCS251 contract violation: MCS251 bit object 'flag': handle escape (ordinary load/store/GEP/cast/ptrtoint/instruction use)
;ESCAPEGEP:  LLVM ERROR: MCS251 contract violation: MCS251 bit object 'flag': handle must not escape through a constant expression or initializer
;ESCAPEINIT: LLVM ERROR: MCS251 contract violation: MCS251 bit object 'flag': handle must not escape through a constant expression or initializer

; An ordinary module (no bit-object attribute) keeps its prior behavior.
;RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/ordinary.ll -o %t/ordinary.o
;RUN: llvm-readobj --sections %t/ordinary.o | FileCheck %s --check-prefix=ORD --implicit-check-not=.mcs251.bit

;NOSTORE-NOT: .mcs251.dseg
;ORD-NOT: .mcs251.bit

;--- def.ll
target triple = "mcs251"
@flag = global i8 1 #0
@zero = global i8 0 #0
@local = internal global i8 1 #0
@ext = external global i8 #0
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- bad-init.ll
target triple = "mcs251"
@flag = global i8 2 #0
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- bad-type.ll
target triple = "mcs251"
@flag = global i16 1 #0
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- bad-tls.ll
target triple = "mcs251"
@flag = thread_local global i8 1 #0
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- extern-only.ll
target triple = "mcs251"
@ext = external global i8 #0
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- keepalive.ll
target triple = "mcs251"
@flag = global i8 0 #0
@llvm.used = appending global [1 x ptr] [ptr @flag], section "llvm.metadata"
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- keepalive-mixed.ll
target triple = "mcs251"
@flag = global i8 0 #0
@ordinary = global i8 5
@llvm.used = appending global [2 x ptr] [ptr @flag, ptr @ordinary], section "llvm.metadata"
attributes #0 = { "mcs251-bit-object" }
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- escape-load.ll
target triple = "mcs251"
@flag = global i8 1 #0
attributes #0 = { "mcs251-bit-object" }
define i8 @f() {
  %v = load i8, ptr @flag
  ret i8 %v
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- escape-gep.ll
target triple = "mcs251"
@flag = global i8 1 #0
attributes #0 = { "mcs251-bit-object" }
define ptr @f() {
  ret ptr getelementptr (i8, ptr @flag, i16 1)
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- escape-ptrtoint.ll
target triple = "mcs251"
@flag = global i8 1 #0
attributes #0 = { "mcs251-bit-object" }
@alias = global i16 ptrtoint (ptr @flag to i16)
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- aggregate-return.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
@llvm.used = appending global [1 x ptr] [ptr @flag], section "llvm.metadata"
define [1 x ptr] @f() { ret [1 x ptr] [ptr @flag] }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- aggregate-export.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
@llvm.used = appending global [1 x ptr] [ptr @flag], section "llvm.metadata"
@export = global [1 x ptr] [ptr @flag]
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- cast-shared.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
@llvm.used = appending addrspace(1) global [1 x ptr addrspace(1)] [ptr addrspace(1) addrspacecast (ptr @flag to ptr addrspace(1))], section "llvm.metadata"
define ptr addrspace(1) @f() { ret ptr addrspace(1) addrspacecast (ptr @flag to ptr addrspace(1)) }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0, i32 0}
;--- fake-intrinsic.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.fake(ptr)
define void @f() { call void @llvm.mcs251.bit.fake(ptr @flag)
  ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- bundle-intrinsic.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.set(i32)
define void @f() { call void @llvm.mcs251.bit.set(i32 7) [ "escape"(ptr @flag) ]
  ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- ordinary.ll
target triple = "mcs251"
@ordinary = global i8 7
define void @f() { ret void }

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
