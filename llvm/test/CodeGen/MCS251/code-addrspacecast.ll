; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O2 -verify-machineinstrs < %s | FileCheck %s
;
; A3 (RUNTIME-AS-PTR-DESIGN-A.md §3-A3, DESIGN.md B.1.1/B.2.1.2/D.4): the
; restricted, equal-width AS4 <-> 32-bit AS0 addrspacecast.
;
; The tests use *dynamic* pointer parameters on purpose: a test over a folded
; global address would never reach LowerAddrSpaceCast (the cast folds into a
; ConstantExpr or is selected directly), so it would not verify the runtime
; conversion at all. Every function here forces a real cast node.
;
; Ruling: AS4 is deliberately NOT added to IsFarRAM -- that set feeds i32
; equal-width RAM interconversion, i16->i32 extension and i32->i16 constant
; narrowing, and joining it would silently open AS4<->AS3/AS9, Near->CODE and
; low-address CODE constant narrowing, none of which are approved.

; AS4 -> AS0 (32-bit): representation pass-through. The value exists once in
; the pointer registers; no extra instruction is emitted for the cast itself.
define ptr @code_to_plain(ptr addrspace(4) %p) {
; CHECK-LABEL: _code_to_plain:
; CHECK-NOT:   #APP
; CHECK:       eret
  %q = addrspacecast ptr addrspace(4) %p to ptr
  ret ptr %q
}

; AS0 -> AS4 (explicit conversion): same pass-through in the other direction.
define ptr addrspace(4) @plain_to_code(ptr %p) {
; CHECK-LABEL: _plain_to_code:
; CHECK:       eret
  %q = addrspacecast ptr %p to ptr addrspace(4)
  ret ptr addrspace(4) %q
}

; The converted alias reads through the SAME channel as a direct AS4 load:
; compare the loading function pair below byte for byte.
define i8 @direct_code_load(ptr addrspace(4) %p) {
; CHECK-LABEL: _direct_code_load:
; CHECK:       mov {{r[0-9]+}}, @dr{{[0-9]+}}
; CHECK:       eret
  %v = load i8, ptr addrspace(4) %p, align 1
  ret i8 %v
}

define i8 @via_plain_load(ptr addrspace(4) %p) {
; CHECK-LABEL: _via_plain_load:
; CHECK:       mov {{r[0-9]+}}, @dr{{[0-9]+}}
; CHECK:       eret
  %q = addrspacecast ptr addrspace(4) %p to ptr
  %v = load i8, ptr %q, align 1
  ret i8 %v
}

; A multi-byte access keeps the same width through the conversion.
define i32 @via_plain_load32(ptr addrspace(4) %p) {
; CHECK-LABEL: _via_plain_load32:
; CHECK:       mov {{r[0-9]+}}, @dr{{[0-9]+}}
; CHECK:       eret
  %q = addrspacecast ptr addrspace(4) %p to ptr
  %v = load i32, ptr %q, align 1
  ret i32 %v
}

; Bank preservation: the 24-bit address (bank byte included) is not truncated
; or re-tagged by the conversion.
define i32 @roundtrip_bank(i32 %addr) {
; CHECK-LABEL: _roundtrip_bank:
; CHECK-NOT:   and
; CHECK-NOT:   trunc
; CHECK:       eret
  %p = inttoptr i32 %addr to ptr addrspace(4)
  %q = addrspacecast ptr addrspace(4) %p to ptr
  %v = ptrtoint ptr %q to i32
  ret i32 %v
}
