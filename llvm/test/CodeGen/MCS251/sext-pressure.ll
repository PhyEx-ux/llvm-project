; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=FI
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=twoaddressinstruction %s -o - | FileCheck %s --check-prefix=TA
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=greedy %s -o - | FileCheck %s --check-prefix=RA

; Register-pressure and multi-use coverage for the sign-extension lowering.
;
; Final-assembly checks are shape-level only (post-RA copies/coalescing may
; legally move values). The source-preservation properties are bound as MIR
; DATAFLOW in two stages:
;   FI (-stop-after=finalize-isel): the SSA view before
;   TwoAddressInstructionPass -- loads, REG_SEQUENCE assembly, extraction
;   chains, and the tied XOR still reading the original vregs.
;   TA (-stop-after=twoaddressinstruction): after that pass -- every
;   REG_SEQUENCE is gone and every tied XOR reads a prepended working COPY
;   whose vreg is both the XOR's input AND its output; the original source
;   must still be consumed afterwards (the store).
; The spill case binds, at RA (-stop-after=greedy; VirtRegRewriter runs
; later, so vregs are still visible), the sext ADD result parked in one
; complete-DR frame slot and reloaded from the SAME slot into the post-call
; use.
;
; First-lit note (anticipated, per review): if the MIR printer spells
; extraction subregisters as `.sub_lo16`-style operands instead of the
; colon form on COPY sources, only those line patterns need rewording; the
; captured vreg references stay as they are.

@gv8  = external global i8
@gv16 = external global i16
@gv32 = external global i32

declare i32 @f32c(i32)

;-----------------------------------------------------------------------------
; Spill of the extension result itself: %v is passed as a complete i32
; argument to an external side-effecting call (not marked readonly, so the
; call cannot be dropped) and consumed again AFTER the call. Every GPR
; except the stack pointer is caller-saved (MCS251RegisterInfo.cpp:29-44),
; so %v -- defined by the non-rematerialisable bias ADD -- must survive the
; call through a full-DR spill. The old shape (values merely live around
; unrelated calls) is not sufficient: the pointer was dead after the load,
; so the whole extension could legally have been sunk after the calls, and
; a matched spill might have belonged to a sibling value.
;-----------------------------------------------------------------------------

define i32 @sext_spill(ptr %p) {
; CHECK-LABEL: _sext_spill:
; CHECK-DAG: xrl r{{[0-9]+}}, #0x80
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; %v is consumed as a full i32 argument: all four ABI bytes, then the spill
; (anywhere ahead of the call), then the call
; CHECK-DAG: mov dpl, r{{[0-9]+}}
; CHECK-DAG: mov dph, r{{[0-9]+}}
; CHECK-DAG: mov b, r{{[0-9]+}}
; CHECK-DAG: mov a, r{{[0-9]+}}
; CHECK-DAG: mov @dr60{{[^,]*}}, wr{{[0-9]+$}}
; CHECK: ecall _f32c
; CHECK: mov wr{{[0-9]+}}, @dr60{{[^,]*}}{{$}}
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: eret
; O0-LABEL: _sext_spill:
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0xff80
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: mov @dr60{{[^,]*}}, wr{{[0-9]+$}}
; O0: ecall _f32c
; O0: mov wr{{[0-9]+}}, @dr60{{[^,]*}}{{$}}
; RA-LABEL: name: sext_spill
; the bias ADD's result vreg is the value parked across the call...
; RA: %[[V:[0-9]+]]:gpr32 = ADD32rr %{{[0-9]+}}, %{{[0-9]+}}
; RA: MOV32mrF $dr60, %stack.[[S:[0-9]+]], 0, {{(killed )?}}%[[V]]
; RA: ECALL @f32c
; ...and reloaded from the SAME slot into the post-call use
; RA: %[[V2:[0-9]+]]:gpr32 = MOV32rmF $dr60, %stack.[[S]]
; RA: ADD32rr {{.*}}%[[V2]]
; RA: ERET
entry:
  %d = load i8, ptr %p
  %v = sext i8 %d to i32
  %r = call i32 @f32c(i32 %v)
  %s = add i32 %v, %r
  ret i32 %s
}

;-----------------------------------------------------------------------------
; Multi-use i8 -> i16: the narrow store's ADDRESS is computed from the wide
; extension result (ADD -> store data dependency), and the store DATA is
; the original byte -- so the original must stay live across the flip.
;-----------------------------------------------------------------------------

define i16 @sext_multiuse8(ptr %p) {
; CHECK-LABEL: _sext_multiuse8:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: add wr{{[0-9]+}}, #0xff80
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: eret
; O0-LABEL: _sext_multiuse8:
; O0: mov r{{[0-9]+}}, #0x00
; O0: xrl r{{[0-9]+}}, #0x80
; O0: add wr{{[0-9]+}}, #0xff80
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; closes the RA region of sext_spill (no RA checks from here on)
; RA-LABEL: name: sext_multiuse8{{$}}
; anchored: "name: sext_multiuse8" is a prefix of the _8_32 name
; FI-LABEL: name: sext_multiuse8{{$}}
; before TwoAddressInstructionPass the tied XOR reads the load's own vreg
; FI: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[S]], -128
; FI: %{{[0-9]+}}:gpr16 = ADD16ri %{{[0-9]+}}, -128
; FI: ERET
; TA-LABEL: name: sext_multiuse8{{$}}
; the working COPY's vreg is both the XOR's input and its output...
; TA: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; TA: %[[W:[0-9]+]]:gpr8 = COPY %[[S]]
; TA: %[[W]]:gpr8 = XOR8ri %[[W]], -128
; ...and the original byte is still the store's data operand, after the flip
; TA: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[S]]
; TA: ERET
entry:
  %c = load i8, ptr %p
  %e = sext i8 %c to i16
  %ew = zext i16 %e to i32
  %q = getelementptr i8, ptr %p, i32 %ew
  store i8 %c, ptr %q
  ret i16 %e
}

; Multi-use i8 -> i32: the store address consumes the extension's ADD32
; result directly (i8-scaled GEP), the data is the original byte. The O0
; block below is shape-level only; the source-preservation dataflow is the
; O2 TA layer above and is not claimed for O0.
define i32 @sext_multiuse8_32(ptr %p) {
; CHECK-LABEL: _sext_multiuse8_32:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: eret
; O0-LABEL: _sext_multiuse8_32:
; O0: mov r{{[0-9]+}}, #0x00
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0xff80
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; FI-LABEL: name: sext_multiuse8_32
; FI: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[S]], -128
; the extension result feeds the store's address computation
; FI: %[[E:[0-9]+]]:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%{{[0-9]+}}
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[E]]
; FI: ERET
; TA-LABEL: name: sext_multiuse8_32
; TA: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; TA: %[[W:[0-9]+]]:gpr8 = COPY %[[S]]
; TA: %[[W]]:gpr8 = XOR8ri %[[W]], -128
; TA: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[S]]
; TA: ERET
entry:
  %c = load i8, ptr %p
  %e = sext i8 %c to i32
  %q = getelementptr i8, ptr %p, i32 %e
  store i8 %c, ptr %q
  ret i32 %e
}

; Multi-use i16 -> i32: same construction at i16 granularity; BOTH original
; lanes (hi = sign byte, lo) must survive the flip and reappear as the
; store's data operands.
define i32 @sext_multiuse16(ptr %p) {
; CHECK-LABEL: _sext_multiuse16:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0x8000
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; CHECK: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; CHECK: eret
; O0-LABEL: _sext_multiuse16:
; O0: mov r{{[0-9]+}}, #0x00
; O0: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; O0-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0x8000
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: mov @dr{{[0-9]+}}, r{{[0-9]+$}}
; O0: mov @dr{{[0-9]+}}+0x0001, r{{[0-9]+}}
; FI-LABEL: name: sext_multiuse16
; the loaded word assembles with the disp-0 byte in the sub_hi8 (sign)
; lane; the flip is the extraction of THAT lane, before TwoAddress
; FI: %[[HI0:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI-NEXT: %[[LO0:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 1
; FI: %{{[0-9]+}}:gpr16 = REG_SEQUENCE {{(killed )?}}%[[HI0]], %subreg.sub_hi8, {{(killed )?}}%[[LO0]], %subreg.sub_lo8
; FI: %[[X:[0-9]+]]:gpr8 = COPY %{{[0-9]+}}.sub_hi8
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[X]], -128
; FI: %[[E:[0-9]+]]:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%{{[0-9]+}}
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[E]]
; FI: ERET
; TA-LABEL: name: sext_multiuse16
; after REG_SEQUENCE elimination the extractions fold to plain COPYs off
; the two load vregs themselves
; TA: %[[HI0:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; TA-NEXT: %[[LO0:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 1
; TA: %[[LO:[0-9]+]]:gpr8 = COPY {{(killed )?}}%[[LO0]]
; TA: %[[HI:[0-9]+]]:gpr8 = COPY {{(killed )?}}%[[HI0]]
; the destructive XOR works on a working copy of the sign lane...
; TA: %[[WC:[0-9]+]]:gpr8 = COPY %[[HI]]
; TA: %[[WC]]:gpr8 = XOR8ri %[[WC]], -128
; ...while both ORIGINAL lanes are still the store's data operands
; TA: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 0, {{(killed )?}}%[[HI]]
; TA: MOV8mrP {{(killed )?}}%{{[0-9]+}}, 1, {{(killed )?}}%[[LO]]
; TA: ERET
entry:
  %s = load i16, ptr %p
  %e = sext i16 %s to i32
  %q = getelementptr i8, ptr %p, i32 %e
  store i16 %s, ptr %q
  ret i32 %e
}

;-----------------------------------------------------------------------------
; CSE shape: two extensions in one function. Identical machine nodes (zero
; lanes, the 0xffffff80 bias) may be CSE-shared between the two sequences;
; that sharing is a semantic non-event (pure uses) and NO claim is made
; here that it proves FastRA lane safety. The two source bytes come from
; two plain loads with NO dependency between the chains (the gep's +1 is
; folded into the addressing displacement by parseAddress, so the second
; load is an offset-1 access, not a derived base). The two extension
; results are consumed incommensurably -- one as the return value, one
; through a volatile wide store -- because a symmetric mix (e.g. xor)
; lets the DAG combiner fuse sext(a) OP sext(b) into a single
; sext(a OP b) and the second sequence disappears (observed on the first
; lit run).
;-----------------------------------------------------------------------------

define i32 @sext_cse_pair(ptr %p) {
; CHECK-LABEL: _sext_cse_pair:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; CHECK: eret
; O0-LABEL: _sext_cse_pair:
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0xff80
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0: xrl r{{[0-9]+}}, #0x80
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; closes the TA region of sext_multiuse16 (no TA checks here)
; TA-LABEL: name: sext_cse_pair{{$}}
; FI-LABEL: name: sext_cse_pair
; two independent load -> flip chains; order between them is not asserted
; FI-DAG: %[[C1:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 0
; FI-DAG: %{{[0-9]+}}:gpr8 = XOR8ri %[[C1]], -128
; FI-DAG: %[[C2:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 1
; FI-DAG: %{{[0-9]+}}:gpr8 = XOR8ri %[[C2]], -128
; FI: ERET
entry:
  %c1 = load i8, ptr %p
  %v1 = sext i8 %c1 to i32
  %q = getelementptr i8, ptr %p, i32 1
  %c2 = load i8, ptr %q
  %v2 = sext i8 %c2 to i32
  store volatile i32 %v2, ptr @gv32
  ret i32 %v1
}
