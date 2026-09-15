; G7 S1' end-to-end shape of demo 36 (src/36-一线制温度传感器 DS18B20 测温/
; main.c:167-174, ZERO corpus change).  The two branches of ReadTemperature
; are reproduced verbatim from the clang -cc1 -O0 -fmcs251-keil IR:
;
;   negative branch: Temperature = ~Temperature + 1; Temperature *= 0.625;
;     -> uitofp i16 -> fmul f32 -> fptoui i16   (the exact f32 gap: unsigned
;        AND narrow in one node, previously "f32 conversion is not in the
;        connected libcall subset")
;   positive branch: Temperature = ((TempH<<8)|TempL) * 0.625;
;     -> sitofp i32 -> fmul f32 -> fptoui i16   (already connected before S1')
;
; The whole chain must now lower: the narrow forms promote to the i32 helpers
; through the generic soft-float legalizer, so the emitted call chain is
; __floatunsisf -> __mulsf3 (four-byte _PARM_2 slot) -> __fixunssfsi, and the
; v2 object's Tag 28 signature table must carry all three helper records.
;
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs %t/demo36.ll -o - | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs %t/demo36.ll -o - | FileCheck %s --check-prefix=O2
; RUN: llc -mtriple=mcs251 -O2 -verify-machineinstrs -mcs251-object-format=elf -filetype=obj %t/demo36.ll -o %t.o
; RUN: llvm-readobj --symbols --arch-specific %t.o | FileCheck %s --check-prefix=SIG

; O0-LABEL: _read_neg:
; O0: ecall __floatunsisf
; O0: __mulsf3_PARM_2
; O0: ecall __mulsf3
; O0: ecall __fixunssfsi
; O0: eret
; O0-LABEL: _read_pos:
; O0: ecall __floatsisf
; O0: __mulsf3_PARM_2
; O0: ecall __mulsf3
; O0: ecall __fixunssfsi
; O0: eret

; O2-LABEL: _read_neg:
; O2: ecall __floatunsisf
; O2: __mulsf3_PARM_2
; O2: ecall __mulsf3
; O2: ecall __fixunssfsi
; O2: eret
; O2-LABEL: _read_pos:
; O2: ecall __floatsisf
; O2: __mulsf3_PARM_2
; O2: ecall __mulsf3
; O2: ecall __fixunssfsi
; O2: eret

; The helper symbols are recorded as external functions in the object: the
; Tag 28 signature table appends one record per backend-generated libcall from
; MCS251HelperABI.h, so each helper must appear (a) in the ELF symbol table as
; an UND global and (b) as a NUL-terminated name inside the Tag 28 BYTES
; payload.  readobj does not decode the payload's record structure (see
; elf-v2-identity.ll), so the payload is checked through llvm-strings, which
; extracts exactly the embedded symbol names; the backend would have
; hard-errored on an unregistered helper ABI before emitting anything.
; SIG: Name: __floatunsisf
; SIG: Name: __mulsf3
; SIG: Name: __fixunssfsi
; RUN: llvm-strings %t.o | FileCheck %s --check-prefix=TAG28
; TAG28-DAG: __floatunsisf
; TAG28-DAG: __mulsf3
; TAG28-DAG: __fixunssfsi

;--- demo36.ll
@TempH = dso_local global i16 0, align 1
@TempL = dso_local global i16 0, align 1
@Temperature = dso_local global i16 0, align 1

; main.c:167-171 -- the sign-fold then scale path (raw u16 arithmetic).
define dso_local void @read_neg() addrspace(4) {
  %1 = load volatile i16, ptr @TempH, align 1
  %2 = zext i16 %1 to i32
  %3 = shl i32 %2, 8
  %4 = load volatile i16, ptr @TempL, align 1
  %5 = zext i16 %4 to i32
  %6 = or i32 %3, %5
  %7 = trunc i32 %6 to i16
  store i16 %7, ptr @Temperature, align 1
  %8 = load i16, ptr @Temperature, align 1
  %9 = zext i16 %8 to i32
  %10 = xor i32 %9, -1
  %11 = add nsw i32 %10, 1
  %12 = trunc i32 %11 to i16
  store i16 %12, ptr @Temperature, align 1
  %13 = load i16, ptr @Temperature, align 1
  %14 = uitofp i16 %13 to float
  %15 = fmul float %14, 6.250000e-01
  %16 = fptoui float %15 to i16
  store i16 %16, ptr @Temperature, align 1
  ret void
}

; main.c:174 -- the positive branch scales the i32-promoted raw value.
define dso_local void @read_pos() addrspace(4) {
  %1 = load volatile i16, ptr @TempH, align 1
  %2 = zext i16 %1 to i32
  %3 = shl i32 %2, 8
  %4 = load volatile i16, ptr @TempL, align 1
  %5 = zext i16 %4 to i32
  %6 = or i32 %3, %5
  %7 = sitofp i32 %6 to float
  %8 = fmul float %7, 6.250000e-01
  %9 = fptoui float %8 to i16
  store i16 %9, ptr @Temperature, align 1
  ret void
}

!mcs251.signatures = !{!0, !1, !2}
!0 = !{!"_read_neg", i32 9, i32 0, i32 0}
!1 = !{!"_read_pos", i32 9, i32 0, i32 0}
!2 = !{!"_Temperature", i32 2, i32 0, i32 0}
