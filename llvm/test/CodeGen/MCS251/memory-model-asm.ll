; RUN: llc -mtriple=mcs251 -mcs251-memory-model=tiny -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V2,TINY
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=xtiny -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V2,TINY
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=small -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V2,SMALL
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=large -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V2,SMALL
; RUN: llc -mtriple=mcs251 -mcs251-memory-model=xsmall -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V1,SMALL
; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s --check-prefixes=V1,SMALL
;
; Each named model selects its numeric contract: tiny/xtiny carry the 16-bit
; AS0 v2 layout, small/xsmall/large the 32-bit one. Only the 32-bit
; InternalExtended profile (xsmall, also the unspecified default) downgrades
; to the implemented v1 object identity; the other v2 profiles carry the
; inspection-only module marker.

; V2: .mcs251_v2_nonobject
; V2-NOT: .optsdcc
; V1: .optsdcc
; V1-NOT: .mcs251_v2_nonobject

define i16 @pointer_size() {
; TINY-LABEL: _pointer_size:
; TINY: mov {{wr[0-9]+}}, #0x0002
; SMALL-LABEL: _pointer_size:
; SMALL: mov {{wr[0-9]+}}, #0x0004
  %p = getelementptr ptr, ptr null, i16 1
  %n = ptrtoint ptr %p to i16
  ret i16 %n
}
