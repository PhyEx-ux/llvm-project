; G11-B: the dedicated fixed sections of placed entities (design
; G11-PLACEMENT-DESIGN.md rev 7, §3.2).
;
; Every entity carrying the G11-A "mcs251-place" IR attribute emits into its
; OWN `.mcu.fixed.<stable-symbol>` section -- one entity per section, never
; merged even for adjacent addresses:
;   * storage_class data (AS0-DATA)  -> SHT_NOBITS,   ALLOC|WRITE
;   * storage_class xdata            -> SHT_NOBITS,   ALLOC|WRITE
;   * storage_class code object      -> SHT_PROGBITS, ALLOC (no EXECINSTR)
;   * storage_class code function    -> SHT_PROGBITS, ALLOC|EXECINSTR
; The single defined symbol of each section sits at st_value 0 (the linker's
; NOTE placement gives the entity entry == A); an object's st_size equals
; the section sh_size, a function's st_size stays the body-label span while
; sh_size also covers the same-section payload (see placement-function-span).
; The sparse XINIT / xdata_init init records of an initialized placed object
; keep the ordinary v1 shapes, with the destination relocation naming the
; entity symbol (resolved to A by the G11-C linker).
;
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o %t.o
; RUN: llvm-readobj --sections --symbols --relocations %t.o | FileCheck %s --check-prefix=ELF
;
; Sections (functions are emitted before the doFinalization globals; the
; section order is the emission order).
; ELF: Name: .mcu.fixed.fp
; ELF: Type: SHT_PROGBITS
; ELF: Flags [ (0x6)
; ELF: Size: 45
; ELF: Name: .mcu.fixed.dv
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 4
; ELF: AddressAlignment: 1
; ELF: Name: .mcs251.xinit
; ELF: Name: .rela.mcs251.xinit
; ELF: Name: .mcu.fixed.xv
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 2
; ELF: Name: .rela.mcs251.xdata_init
; ELF: Name: .mcu.fixed.cv
; ELF: Type: SHT_PROGBITS
; ELF: Flags [ (0x2)
; ELF: Size: 4
; ELF: Name: .mcu.fixed.al
; ELF: Type: SHT_NOBITS
; ELF: Flags [ (0x3)
; ELF: Size: 4
; ELF: AddressAlignment: 4
; ELF: Name: .mcs251.placement
; ELF: Type: SHT_NOTE
;
; The init records keep the ordinary v1 shapes with the entity symbol as
; the destination (readobj prints Sections, Relocations, then Symbols).
; ELF: R_MCS251_16 _dv
; ELF: R_MCS251_16 _al
; ELF: R_MCS251_HI8 _xv
; ELF: R_MCS251_MID8 _xv
; ELF: R_MCS251_LO8 _xv
;
; One defined sized symbol per fixed section, st_value 0, in the entity's
; own section only.
; ELF: Name: _fp
; ELF: Value: 0x0
; ELF: Type: Function
; ELF: Section: .mcu.fixed.fp
; ELF: Name: _dv
; ELF: Value: 0x0
; ELF: Size: 4
; ELF: Type: Object
; ELF: Section: .mcu.fixed.dv
; ELF: Name: _xv
; ELF: Value: 0x0
; ELF: Size: 2
; ELF: Type: Object
; ELF: Section: .mcu.fixed.xv
; ELF: Name: _cv
; ELF: Value: 0x0
; ELF: Size: 4
; ELF: Type: Object
; ELF: Section: .mcu.fixed.cv
; ELF: Name: _al
; ELF: Value: 0x0
; ELF: Size: 4
; ELF: Type: Object
; ELF: Section: .mcu.fixed.al
;
; REL objects and assembly text are not placement carriers.
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOELF
; RUN: not llc -mtriple=mcs251 -O0 -filetype=asm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOELF
; NOELF: fixed placement requires ELF object output

target triple = "mcs251-unknown-none"

@dv = global i32 305419896, align 1 #0
@xv = addrspace(3) global i16 4951, align 1 #1
@cv = addrspace(4) constant [4 x i8] c"\11\22\33\44", align 1 #2
@al = global i32 7, align 4 #3

define i32 @fp(i32 %a) addrspace(4) #4 {
entry:
  %r = add i32 %a, 7
  ret i32 %r
}

!mcs251.signatures = !{!10000}
!10000 = !{!"_fp", i32 1, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0x30,data,object,owned,0" "mcs251-stable-symbol"="dv" }
attributes #1 = { "mcs251-place"="0x10000,xdata,object,owned,0" "mcs251-stable-symbol"="xv" }
attributes #2 = { "mcs251-place"="0xFC2800,code,object,owned,0" "mcs251-stable-symbol"="cv" }
attributes #3 = { "mcs251-place"="0x90,data,object,owned,0" "mcs251-stable-symbol"="al" }
attributes #4 = { "mcs251-place"="0xFC3000,code,function,owned,0" "mcs251-stable-symbol"="fp" }
