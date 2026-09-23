; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/as3.ll -o %t/as3.o
; RUN: llvm-readobj --sections --section-data --relocations %t/as3.o | FileCheck %s --check-prefix=AS3
; RUN: llvm-readobj --file-headers %t/as3.o | FileCheck %s --check-prefix=V2
; RUN: llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/as4.ll -o %t/as4.o
; RUN: llvm-readobj --sections --section-data --relocations %t/as4.o | FileCheck %s --check-prefix=AS4
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/f64.ll -o %t/f64.o 2>&1 | FileCheck %s --check-prefix=F64
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/inttoptr.ll -o %t/inttoptr.o 2>&1 | FileCheck %s --check-prefix=INTPTR
; RUN: not llc -mtriple=mcs251 -mcs251-object-format=elf -filetype=obj %t/undef.ll -o %t/undef.o 2>&1 | FileCheck %s --check-prefix=UNDEF
;
; WP5 A3 parity (v2 identity): a global aggregate that combines a binary32
; leaf with a placed-storage (AS3/AS4) pointer leaf is a registered v2
; object. The float member is the 4-byte IEEE-754 bit pattern written through
; the same initializer channel as an i32, so the shared placement walk
; (hasV1PlacementInitializer) must admit it exactly like the storage
; whitelist does; before the parity fix the sibling float made the whole
; aggregate fail the walk and the object-identity gate rejected it.
;
; The DSEG image is the sparse XINIT record (u16 addr, u16 size, u16 paylen,
; payload). @g is 8 bytes: the big-endian float 1.5f = 3f c0 00 00 followed
; by the 4-byte pointer container (MSB literal zero, then the
; R_MCS251_24 field of @x at record offset 0xB).
; AS3: Name: .mcs251.xinit
; AS3: SectionData (
; AS3-NEXT:     0000: 00000008 00083FC0 00000000 0000
; AS3-NEXT:   )
; AS3: 0x0 R_MCS251_16 _g 0x0
; AS3: 0xB R_MCS251_24 _x 0x0
;
; The AS4 (@h) case is the same shape over a CODE-space base; 2.5f is
; 40 20 00 00.
; AS4: Name: .mcs251.xinit
; AS4: SectionData (
; AS4-NEXT:     0000: 00000008 00084020 00000000 0000
; AS4-NEXT:   )
; AS4: 0x0 R_MCS251_16 _h 0x0
; AS4: 0xB R_MCS251_24 _c 0x0
;
; The object is published under the registered v2 identity (e_flags 0x102).
; V2: Flags [ (0x102)
;
; True f64 stays outside the whitelist: the doubling of the payload width is
; not part of the WP5 A3 slice, so the aggregate keeps the rejection.
; F64: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity
;
; inttoptr keeps the actionable absolute-address boundary from the structural
; contract check; the float sibling changes nothing.
; INTPTR: LLVM ERROR: MCS251 contract violation: global 'g': absolute-address (integer-to-pointer cast) pointer initialization is not supported
;
; undef is not an emittable leaf: the placement walk never admits it.
; UNDEF: LLVM ERROR: MCS251: module uses an ABI capability outside the registered A4 v2 object identity

;--- as3.ll
%struct.S = type { float, ptr addrspace(3) }

@x = addrspace(3) global i32 0, align 1
@g = global %struct.S { float 1.500000e+00, ptr addrspace(3) @x }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- as4.ll
%struct.T = type { float, ptr addrspace(4) }

@c = addrspace(4) constant i8 0, align 1
@h = global %struct.T { float 2.500000e+00, ptr addrspace(4) @c }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- f64.ll
%struct.D = type { double, ptr addrspace(3) }

@x = addrspace(3) global i32 0, align 1
@g = global %struct.D { double 1.500000e+00, ptr addrspace(3) @x }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- inttoptr.ll
%struct.S = type { float, ptr addrspace(3) }

@g = global %struct.S { float 1.500000e+00, ptr addrspace(3) inttoptr (i32 16 to ptr addrspace(3)) }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
;--- undef.ll
%struct.S = type { float, ptr addrspace(3) }

@x = addrspace(3) global i32 0, align 1
@g = global %struct.S { float undef, ptr addrspace(3) @x }, align 1

define void @f() {
  ret void
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_f", i32 1, i32 0}
