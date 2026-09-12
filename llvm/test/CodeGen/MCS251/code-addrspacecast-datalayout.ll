; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 -verify-machineinstrs < %s | FileCheck %s --check-prefix=MOD
; RUN: llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 -filetype=obj -mcs251-object-format=elf < %s -o %t.o && llvm-readobj --file-headers %t.o | FileCheck %s --check-prefix=ELF
; RUN: opt -passes=instcombine,gvn,simplifycfg -S < %s | FileCheck %s --check-prefix=OPT
; RUN: opt -mcs251-memory-contract=1,1,32,8,1 -passes='default<O2>' -S < %s | FileCheck %s --check-prefix=O2DYN
; RUN: %python %S/Inputs/check-as4-layout-dataflow.py --llc llc --opt opt --test-ll %s
; RUN: %python %S/Inputs/check-as4-layout-dataflow.py --llc llc --opt opt --test-ll %s --self-test
;
; A3 §2.2 (RUNTIME-AS-PTR-DESIGN-A.md): the upstream no-op assumption for
; addrspacecast (BasicAA strips it when the source is a scalar pointer with
; equal index width) is only sound while DataLayout, pointer size, index size
; and the actual lowering agree for AS0 and AS4.
;
; This test no longer *declares* that agreement as a hard-coded input (the
; previous version did, so it could not catch a real target mismatch).  The
; layout facts are now QUERIED from the backend by
; Inputs/check-as4-layout-dataflow.py, which:
;   * lets llc fill an empty module per memory contract and parses the
;     injected datalayout, so p0/p4 size and index width come from the
;     target itself (v1 compat, v2 32-bit, and the 16-bit Tiny/XTiny refusal
;     controls -- which are now also RUN, not merely grepped);
;   * runs a real optimization pipeline over THIS file and requires the
;     documented round trip to fold to `ret i32 %addr` -- a mutation that
;     adds 256 is a FAIL;
;   * constrains the ONE-WAY conversion's read address: the converted AS0
;     load must read the same address the AS4 source names (Alice review R9:
;     an inserted `getelementptr ... 256` used to pass);
;   * requires the O2 pipeline to keep a dynamic observation: `@ext_src` is
;     an external AS4 object so the address cannot be folded, and the
;     converted load must survive as a real `load i32, ptr %...` (the old
;     folded `icmp eq i32 ptrtoint @obj, ptrtoint (addrspacecast @obj)` was
;     not a run-time data-flow observation);
;   * rejects bitcast/ptrtoint laundering of the conversion;
;   * counts DR lanes on the direct AS4 load and the converted AS0 load and
;     requires both to be 4 for the i32 load.
;
; The `target datalayout` below is deliberately the *minimal* v1-compatible
; layout: it is the input the old test pinned, and it must keep being
; accepted and re-emitted as the target's own layout.

target datalayout = "E-m:s-p:32:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16:32-S8"
target triple = "mcs251-unknown-none"

; The external AS4 object read below.  It is `external` so the optimizer
; cannot resolve its address and fold the converted load into a constant:
; the O2 pipeline must leave a genuine dynamic load.  The definition lives in
; the e2e firmware (src/a3-layout-fw.c), which links a real `lay_tab`.
@ext_src = external addrspace(4) constant [8 x i8]

; The round trip must be a pure pass-through: AS4 -> AS0 -> AS4 returns the
; original address.  Under `instcombine,gvn,simplifycfg` it must fold to
; `ret i32 %addr`; any masking, extension or arithmetic on the way (a `+256`,
; a `trunc`, an `and`) breaks the fold and is caught.
define i32 @roundtrip_noop(i32 %addr) {
; OPT-LABEL: define {{.*}}@roundtrip_noop(
; OPT-NEXT:    ret i32 %addr
; MOD-LABEL: _roundtrip_noop:
; MOD-NOT:   and
; MOD-NOT:   or
; MOD-NOT:   trunc
; MOD-NOT:   zext
; MOD:       eret
  %p4 = inttoptr i32 %addr to ptr addrspace(4)
  %p0 = addrspacecast ptr addrspace(4) %p4 to ptr
  %back = addrspacecast ptr %p0 to ptr addrspace(4)
  %v = ptrtoint ptr addrspace(4) %back to i32
  ret i32 %v
}

; A multi-byte load through the converted alias uses the same DR lane count as
; the direct AS4 load: equal width on both sides, so the conversion cannot
; have changed the access width either.  (The checker independently counts
; the lanes; these lines pin the sequences for human review.)
define i32 @direct_load(ptr addrspace(4) %p) {
; MOD-LABEL: _direct_load:
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0001
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0002
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0003
; O2DYN-LABEL: define {{.*}}i32 @direct_load(
; O2DYN: load i32, ptr addrspace(4) %p
; O2DYN: ret i32
  %v = load i32, ptr addrspace(4) %p, align 1
  ret i32 %v
}

; The one-way conversion: the converted AS0 load must read the very address
; the AS4 parameter names.  An offset inserted on the converted path (Alice
; review R9-1) is a dataflow violation, not a value-preserving conversion.
; O2 must keep this load dynamic (no constant fold), which the external
; object guarantees.
define i32 @via_plain_load(ptr addrspace(4) %p) {
; MOD-LABEL: _via_plain_load:
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0001
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0002
; MOD:       mov {{r[0-9]+}}, @dr{{[0-9]+}}+0x0003
; OPT-LABEL: define {{.*}}i32 @via_plain_load(
; OPT: [[Q:%.*]] = addrspacecast ptr addrspace(4) %p to ptr
; OPT: load i32, ptr [[Q]]
; O2DYN-LABEL: define {{.*}}i32 @via_plain_load(
; O2DYN: [[Q:%.*]] = addrspacecast ptr addrspace(4) %p to ptr
; O2DYN: load i32, ptr [[Q]]
; O2DYN: ret i32
  %q = addrspacecast ptr addrspace(4) %p to ptr
  %v = load i32, ptr %q, align 1
  ret i32 %v
}

; The object-file path must accept the module and keep emitting the v1 ELF
; identity; the ELF32 class alone is NOT accepted as proof of pointer width
; (that claim is now backed by the queried p0/p4 layout in the checker).
; ELF: Format: elf32-mcs251
; ELF: Class: 32-bit
