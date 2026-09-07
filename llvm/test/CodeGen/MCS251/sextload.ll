; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NO16
; RUN: llc -mtriple=mcs251 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=NO16-O0
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -stop-after=finalize-isel %s -o - | FileCheck %s --check-prefix=FI

; SEXTLOAD: the three widening widths (i8->i16, i8->i32, i16->i32).
; LowerLoad forwards the extension kind to SIGN_EXTEND, which lowers through
; the offset-binary bias identity (QEMU P-A2 released forms (a)/(c)/(d)):
;
;     sext_N(x) == zext_N(x ^ 2^(m-1)) + (2^N - 2^(m-1))   (mod 2^N)
;
; Shared shape across the widths: every zero target lane is an explicit
; `mov r,#0x00` (machine-node CSE may share one vreg across lane positions,
; a pure use); the sign byte is flipped with `xrl r,#0x80`; the bias is one
; 16-bit add (`add wr,#0xff80`) or a DR constant pair
; (`mov dr,#0xff80` / `movh dr,#0xffff`) plus `add dr,dr` (i8->i32) or
; `mov dr,#0x8000` / `movh dr,#0xffff` plus `add dr,dr` (i16->i32). The
; REG_SEQUENCE lane assembly emits nothing. The bias add MUST wrap unsigned
; (non-negative inputs produce 2^N + x, which wraps back to x), which the
; plain machine ADD does.
;
; The default (-O2) runs lock the merged SEXTLOAD route; the O0 runs lock
; the unmerged explicit `sext` route through the same lowering core and its
; FastRA lane placement. Lane/register identity is NOT asserted through
; final-assembly captures (post-RA coalescing is free to move values); the
; sign-lane and bias dataflow lives in the FI run at -stop-after=
; finalize-isel, i.e. BEFORE TwoAddressInstructionPass rewrites tied
; operands (it unconditionally prepends a working COPY) and eliminates
; every REG_SEQUENCE; the two-address shape itself is locked in
; sext-pressure.ll. The NO16 runs carry a whole-function check that no i32
; result ever takes the 16-bit sequence. Supersedes the old negative test
; loadstore-error-sextload.ll (removed with this change).
;
; First-lit note (anticipated, per review): the MIR printer spells COPY
; subregister sources in the dot form (`%7.sub_lo16`), prints XOR8ri/ADD16ri
; immediates sign-extended (`-128`), and may prefix operands with `killed`;
; the FI patterns below already use these measured forms.

;-----------------------------------------------------------------------------
; i8 -> i16 : form (a), 3 emitted instructions besides the load
;-----------------------------------------------------------------------------

define i16 @sextload8_16(ptr %p) {
; CHECK-LABEL: _sextload8_16:
; CHECK-DAG: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: add wr{{[0-9]+}}, #0xff80
; O0-LABEL: _sextload8_16:
; O0-DAG: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; O0-DAG: mov r{{[0-9]+}}, #0x00
; O0: xrl r{{[0-9]+}}, #0x80
; O0: add wr{{[0-9]+}}, #0xff80
; NO16-LABEL: _sextload8_16:
; NO16-O0-LABEL: _sextload8_16:
; anchored: "name: sextload8_16" is a prefix of the _cmp name
; FI-LABEL: name: sextload8_16{{$}}
; before TwoAddressInstructionPass the tied XOR reads the load's own vreg
; directly (that pass later prepends a working COPY unconditionally)
; FI: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[S]], -128
; FI: ERET
  %v = load i8, ptr %p
  %e = sext i8 %v to i16
  ret i16 %e
}

;-----------------------------------------------------------------------------
; i8 -> i32 : form (c), direct 32-bit lane assembly {00,00,00,x^80} and the
; full 32-bit bias 0xffffff80. No intermediate i16 sign extension.
;-----------------------------------------------------------------------------

define i32 @sextload8_32(ptr %p) {
; CHECK-LABEL: _sextload8_32:
; CHECK-DAG: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0xff80
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0-LABEL: _sextload8_32:
; O0-DAG: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; O0-DAG: mov r{{[0-9]+}}, #0x00
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0xff80
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _sextload8_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; NO16-O0-LABEL: _sextload8_32:
; NO16-O0-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: sextload8_32
; FI: %[[S:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[S]], -128
; the bias dataflow is fully chained: MOVDRri (low word, clears high) ->
; MOVHDRi input operand -> MOVHDRi result -> the bias ADD. MOV must precede
; MOVH because only MOV clears the high word.
; FI: %[[BL:[0-9]+]]:gpr32 = MOVDRri 65408
; FI: %[[BIAS:[0-9]+]]:gpr32 = MOVHDRi %[[BL]], 65535
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[BIAS]]
; FI: ERET
  %v = load i8, ptr %p
  %e = sext i8 %v to i32
  ret i32 %e
}

;-----------------------------------------------------------------------------
; i16 -> i32 : form (d). The 0x8000 flip lands on the source's big-endian
; Bytes[0] (displacement 0x0000, the sign byte); the +0x0001 byte passes
; through unflipped. Bias 0xffff8000.
;-----------------------------------------------------------------------------

define i32 @sextload16_32(ptr %p) {
; CHECK-LABEL: _sextload16_32:
; CHECK: mov r{{[0-9]+}}, #0x00
; CHECK: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: mov dr{{[0-9]+}}, #0x8000
; CHECK: movh dr{{[0-9]+}}, #0xffff
; CHECK: add dr{{[0-9]+}}, dr{{[0-9]+}}
; O0-LABEL: _sextload16_32:
; O0: mov r{{[0-9]+}}, #0x00
; O0: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; O0-NEXT: mov r{{[0-9]+}}, @dr{{[0-9]+}}+0x0001
; O0: xrl r{{[0-9]+}}, #0x80
; O0: mov dr{{[0-9]+}}, #0x8000
; O0: movh dr{{[0-9]+}}, #0xffff
; O0: add dr{{[0-9]+}}, dr{{[0-9]+}}
; NO16-LABEL: _sextload16_32:
; NO16-NOT: add wr{{[0-9]+}}, #0xff80
; NO16-O0-LABEL: _sextload16_32:
; NO16-O0-NOT: add wr{{[0-9]+}}, #0xff80
; FI-LABEL: name: sextload16_32
; the two byte loads are chained (+0x0000 before +0x0001); the word is
; assembled with the disp-0 byte in the sub_hi8 (sign) lane, and the
; flipped value is the extraction of THAT lane, never the +0x0001 lane
; FI: %[[HI:[0-9]+]]:gpr8 = MOV8rmP {{(killed )?}}%{{[0-9]+}}, 0
; FI-NEXT: %[[LO:[0-9]+]]:gpr8 = MOV8rmP %{{[0-9]+}}, 1
; FI: %[[W:[0-9]+]]:gpr16 = REG_SEQUENCE {{(killed )?}}%[[HI]], %subreg.sub_hi8, {{(killed )?}}%[[LO]], %subreg.sub_lo8
; FI: %[[X:[0-9]+]]:gpr8 = COPY %[[W]].sub_hi8
; FI: %{{[0-9]+}}:gpr8 = XOR8ri %[[X]], -128
; FI: %[[BL:[0-9]+]]:gpr32 = MOVDRri 32768
; FI: %[[BIAS:[0-9]+]]:gpr32 = MOVHDRi %[[BL]], 65535
; FI: %{{[0-9]+}}:gpr32 = ADD32rr %{{[0-9]+}}, {{(killed )?}}%[[BIAS]]
; FI: ERET
  %v = load i16, ptr %p
  %e = sext i16 %v to i32
  ret i32 %e
}

;-----------------------------------------------------------------------------
; Interleaved signed compare: the widened value feeds an i16 signed compare
; whose other operand is a runtime register value (low word of the pointer
; via ptrtoint), so the compare can neither shrink to the i8 domain (the
; operand's high bits are unknown) nor fold to a constant. The extension
; must stay materialized (`add wr,#0xff80`) ahead of the cmp/jcc pair.
;
; The cmp -> jcc adjacency lock: the skip jump must consume the flags of
; THIS compare. Every flag-writing instruction of the backend is named in
; the CHECK-NOT below (full PSW-writer set per MCS251InstrInfo.td: the
; add/sub/anl/orl/xrl/cmp families at 8/16/32 bits, inc/dec, sll/srl/sra,
; mul, rlc/rrc, clr c, ecall); movs, jumps and eret are flag-neutral and
; may legitimately appear in the gap.
;-----------------------------------------------------------------------------

define i8 @sextload8_16_cmp(ptr %p) {
; CHECK-LABEL: _sextload8_16_cmp:
; CHECK-DAG: mov r{{[0-9]+}}, @dr{{[0-9]+$}}
; CHECK-DAG: mov r{{[0-9]+}}, #0x00
; CHECK: xrl r{{[0-9]+}}, #0x80
; CHECK: add wr{{[0-9]+}}, #0xff80
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK-NOT: {{(add|sub|anl|orl|xrl|cmp|inc|dec|sll|srl|sra|mul|rlc|rrc|ecall) |clr c}}
; CHECK: js{{l|ge|g|le}} .LBB{{[0-9_]+}}
; CHECK-NEXT: ejmp .LBB{{[0-9_]+}}
; NO16-LABEL: _sextload8_16_cmp:
; NO16-O0-LABEL: _sextload8_16_cmp:
; closes the FI region of sextload16_32 (this function has no FI checks)
; FI-LABEL: name: sextload8_16_cmp{{$}}
entry:
  %c8 = load i8, ptr %p
  %e = sext i8 %c8 to i16
  %pi = ptrtoint ptr %p to i32
  %w = trunc i32 %pi to i16
  %t = icmp slt i16 %e, %w
  br i1 %t, label %a, label %b
a:
  ret i8 1
b:
  ret i8 2
}

; Same sentinel with an independently loaded unknown i16 as the second
; compare operand (both operands unknown: no shrink, no constant fold, no
; immediate predicate rewrite). A compare against an out-of-domain CONSTANT
; is unusable here: InstCombine's range analysis folds `sext(i8) < -200` to
; a constant false (and an in-domain constant shrinks the compare to i8) --
; that is why both sentinels use runtime operands.
define i8 @sextload8_16_cmp_load(ptr %p) {
; CHECK-LABEL: _sextload8_16_cmp_load:
; CHECK: add wr{{[0-9]+}}, #0xff80
; CHECK: cmp wr{{[0-9]+}}, wr{{[0-9]+}}
; CHECK-NOT: {{(add|sub|anl|orl|xrl|cmp|inc|dec|sll|srl|sra|mul|rlc|rrc|ecall) |clr c}}
; CHECK: js{{l|ge|g|le}} .LBB{{[0-9_]+}}
; CHECK-NEXT: ejmp .LBB{{[0-9_]+}}
; NO16-LABEL: _sextload8_16_cmp_load:
; NO16-O0-LABEL: _sextload8_16_cmp_load:
entry:
  %c8 = load i8, ptr %p
  %e = sext i8 %c8 to i16
  %q = getelementptr i8, ptr %p, i16 2
  %w = load i16, ptr %q
  %t = icmp slt i16 %e, %w
  br i1 %t, label %a, label %b
a:
  ret i8 1
b:
  ret i8 2
}
