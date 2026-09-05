; RUN: llc -mtriple=mcs251 -filetype=obj < %s | FileCheck %s

; Phase 13a (de-SDCC Step 2): llc -filetype=obj writes an ASxxxx XH3 .rel
; directly -- sdas251 is no longer in the production path.  The records are
; the same ones sdas251 would emit for the Step-1 assembly text of this file
; (validated byte-for-byte per field in /tmp/mcs251-p13a):
;
;   * the header carries the locked ABI signature as the O record (the
;     ".optsdcc " keyword belongs to the assembly directive only);
;   * a 16-bit symbol value (mov wr,#_sym) relocates with mode 0x02 against
;     the symbol's S index, a 24-bit ecall target with mode 0x82;
;   * targets DEFINED in this module (even global ones) are area-relative:
;     mode 0x80/0x00 with ref 0001 (CSEG) and the area offset already in the
;     T payload -- exactly sdas251's e_flag/e_base.e_ap split (measured).
;
; The first T line below is obj_load_g in full:
;   mov wr0, #gv8  -> 7E 04 <16-bit reloc payload>
;   mov r0, @wr0   -> 7E 09 00
;   mov dpl, r0    -> 7A 01 82
;   eret           -> AA            (native Area-III opcode, no A5 prefix)

source_filename = "asxxxx-obj.c"

@gv8 = external global i8

declare void @ext_fn()

define i8 @obj_load_g() {
; asm: mov wr0, #gv8 / mov r0, @wr0 / mov dpl, r0 / eret
  %v = load i8, ptr @gv8
  ret i8 %v
}

define void @obj_call_ext() {
  call void @ext_fn()
  ret void
}

define void @obj_call_local() {
  call void @obj_call_ext()
  ret void
}

; CHECK:      XH3
; CHECK-NEXT: H 2 areas 6 global symbols
; CHECK-NEXT: M asxxxx_obj
; CHECK-NEXT: O stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
; CHECK-NEXT: S ext_fn Ref000000
; CHECK-NEXT: S gv8 Ref000000
; CHECK-NEXT: S .__.ABS. Def000000
; CHECK-NEXT: A _CODE size 0 flags 0 addr 0
; CHECK-NEXT: A CSEG size {{[0-9A-F]+}} flags 20 addr 0
; CHECK-NEXT: S obj_load_g Def000000
; CHECK-NEXT: S obj_call_ext Def{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}
; CHECK-NEXT: S obj_call_local Def{{[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]}}
; CHECK-NEXT: T 00 00 00
; CHECK-NEXT: R 00 00 00 01
; T payload is capped at 13 bytes (sdas's NTXT includes the 3 XH3 address
; bytes; sdld's relocation arrays share that limit), so obj_load_g's ecall
; field moves to the next line:
; CHECK-NEXT: T 00 00 00 7E 08 00 00 00 00 00 00 7A 0C 00
; MOV low word: mid/lo byte-of24 occupy 3 T bytes each, but one code byte.
; The second fixup's t-index must therefore be 8, not 6.
; CHECK-NEXT: R 00 00 00 01 F1 83 05 00 01 F1 03 08 00 01
; MOVH high word: constant zero then relocated region byte. Code offset 7,
; not T-payload offset 11: expanded placeholder bytes do not advance PC.
; CHECK-NEXT: T 00 00 07 00 00 00 7E 0B 00 7A 01 82 AA 9A
; CHECK-NEXT: R 00 00 00 01 F3 83 03 00 01
; Direct calls remain full addr24; defined function target remains area-relative.
; CHECK-NEXT: T 00 00 10 00 00 00 AA 9A 00 00 0F AA
; CHECK-NEXT: R 00 00 00 01 82 03 00 00 80 08 00 01
