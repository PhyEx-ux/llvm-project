; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
;
; X2-1/X2-2 adversarial: DPXL (SFR 0x84) mixed with user AS6 SFR-direct
; writes.  The backend's own sequences are self-healing -- every AS3 access
; re-points DPXL from its canonical address immediately before the movx --
; so the "DPXL stays at its reset value 01h" assumption of the phase-1
; lowering is gone.  A user's own AS6 write to 0x84 (DPXL) is a documented
; undefined interaction with backend-generated XDATA accesses (it can change
; the region a subsequent movx would use); what IS guaranteed, and pinned
; here, is that each backend sequence re-points the region itself and never
; inherits the user's value.  Retention protocol: the XDATA-CODE design
; supplement (validation/mcs251-models/proposals/).

; Alice's dp_write probe shape: a user AS6 store of 2 to DPXL immediately
; before an AS3 load.  The AS3 load still emits its own bank byte from the
; canonical pointer and must not rely on the user's 02h.
define i8 @dp_write(ptr addrspace(3) %p) {
; CHECK-LABEL: _dp_write:
; user SFR-direct write: mov 0x84, #2-via-register
; CHECK:         mov r[[U:[0-9]+]], #0x02
; CHECK-NEXT:    mov 0x84, r[[U]]
; the AS3 access re-points the region itself from the pointer lane:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  store volatile i8 2, ptr addrspace(6) inttoptr (i16 132 to ptr addrspace(6))
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}

; Reversed order: an AS3 access followed by a user DPXL write -- the user
; write is emitted verbatim and the next AS3 access (here none) would still
; re-point first.
define void @access_then_write(ptr addrspace(3) %p, i8 %v) {
; CHECK-LABEL: _access_then_write:
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx @dptr, a
; CHECK:         mov r{{[0-9]+}}, #0x03
; CHECK-NEXT:    mov 0x84, r{{[0-9]+}}
  store volatile i8 %v, ptr addrspace(3) %p
  store volatile i8 3, ptr addrspace(6) inttoptr (i16 132 to ptr addrspace(6))
  ret void
}

; SFR writes to OTHER addresses must be untouched by the DPXL discipline.
define i8 @sfr_neighbor(ptr addrspace(3) %p) {
; CHECK-LABEL: _sfr_neighbor:
; CHECK:         mov r[[N:[0-9]+]], #0x5a
; CHECK-NEXT:    mov 0x81, r[[N]]
; CHECK:         mov 0x84, r{{[0-9]+}}
; CHECK:         movx a, @dptr
  store volatile i8 90, ptr addrspace(6) inttoptr (i16 129 to ptr addrspace(6))
  %v = load volatile i8, ptr addrspace(3) %p
  ret i8 %v
}
