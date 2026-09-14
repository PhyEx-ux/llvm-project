; P09 sections 1.1/1.2/1.3 + 7 (P-1a): the symbolic bit-object access family
;   llvm.mcs251.bit.obj.read   -> i1  (ptr) : mov c,bit glued sample group
;   llvm.mcs251.bit.obj.set    -> void(ptr) : setb bit
;   llvm.mcs251.bit.obj.clear  -> void(ptr) : clr  bit
;   llvm.mcs251.bit.obj.toggle -> void(ptr) : cpl  bit
; The single operand is the identity handle: an AS0 i8 GlobalVariable marked
; "mcs251-bit-object". It is never materialised as a byte address -- in
; particular it cannot be printed as assembly text -- so the instruction-level
; assertions read the post-isel MIR (-stop-after=finalize-isel), and the
; object-level assertions (kind-1 record, R_MCS251_BIT_REF, per-use
; R_MCS251_BITADDR8 with a zero addend naming the exact symbol) read the ELF
; object with llvm-readobj. The bit number itself is resolved by the linker.
;
; Instruction selection at both ends of the llc opt-level range (llc has no
; -Os spelling; the O0/O2 pair below brackets the level axis for IR input).
; RUN: split-file %s %t
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O0 -stop-after=finalize-isel %t/pos.ll -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=mcs251 -verify-machineinstrs -O2 -stop-after=finalize-isel %t/pos.ll -o - | FileCheck %s --check-prefix=MIR
;
; The frozen ELF object shape: one 8-byte kind-1 record for the defined
; handle, a BIT_REF association for it, no record for the extern, and one
; zero-addend BITADDR8 per use.
; RUN: llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/pos.ll -o %t.o
; RUN: llvm-readobj --sections --section-data --relocations %t.o | FileCheck %s --check-prefix=ELF --implicit-check-not='R_MCS251_BIT_REF _ext' --implicit-check-not='R_MCS251_16 _flag' --implicit-check-not='R_MCS251_24 _flag'
;
; Independent optimiser pipelines (P09 section 6.2 T10) must preserve the
; access count and the relative order against ordinary byte accesses: no CSE
; of two identical writes, no DCE of an unused read, no reordering across the
; byte store/load. (The default<O1+> pipelines additionally mark tail calls,
; which the frozen section 1.3 A.3 whitelist rejects by design; the negative
; TAIL case below pins that.)
; RUN: opt -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -S -passes='function(instcombine)' %t/pos.ll -o - | FileCheck %s --check-prefix=OPT
; RUN: opt -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -S -passes='function(gvn,instcombine)' %t/pos.ll -o - | FileCheck %s --check-prefix=OPT
;
; Negative matrix (P09 section 6.4 L01-L09, applicable items). One mutated
; factor per fixture; the expected bodies are the frozen section 1.4
; diagnostics, checked with the generic verifier disabled so the TARGET check
; is what speaks (plus one verifier-on variant proving the two layers are
; distinct).

;--- pos.ll
target triple = "mcs251"

@flag = global i8 1 #0
@ext = external global i8 #0
@byte = global i8 0
attributes #0 = { "mcs251-bit-object" }

declare i1 @llvm.mcs251.bit.obj.read(ptr)
declare void @llvm.mcs251.bit.obj.set(ptr)
declare void @llvm.mcs251.bit.obj.clear(ptr)
declare void @llvm.mcs251.bit.obj.toggle(ptr)

; One single bit instruction per writer, naming the handle symbol -- the bit
; address itself is a linker-resolved relocation field.
; MIR-LABEL: name: set_obj
; MIR: SETBBIT @flag
; MIR-NOT: CLRBIT
; MIR-NOT: CPLBIT
; MIR-NOT: MOVCBIT
define void @set_obj() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}

; MIR-LABEL: name: clear_obj
; MIR: CLRBIT @ext
; MIR-NOT: SETBBIT
; MIR-NOT: MOVCBIT
define void @clear_obj() {
  call void @llvm.mcs251.bit.obj.clear(ptr @ext)
  ret void
}

; toggle is a single atomic cpl and never reads the old value first
; (section 1.1): no mov c anywhere in the function.
; MIR-LABEL: name: toggle_obj
; MIR: CPLBIT @flag
; MIR-NOT: MOVCBIT
; MIR-NOT: SETBBIT
; MIR-NOT: CLRBIT
define void @toggle_obj() {
  call void @llvm.mcs251.bit.obj.toggle(ptr @flag)
  ret void
}

; read is one glued sample: the bit is read exactly once (no second mov c, no
; byte load of any backing byte -- the handle has none).
; MIR-LABEL: name: read_obj
; MIR: MOVCBIT @flag, implicit-def $psw
; MIR-NEXT: MOVAI 0, implicit-def $a
; MIR-NEXT: RLCA implicit-def dead $psw, implicit-def $a, implicit $psw, implicit $a
; MIR-NEXT: MOV8ra implicit $a
; MIR-NOT: MOVCBIT
define i8 @read_obj() {
  %b = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  %z = zext i1 %b to i8
  ret i8 %z
}

; Side-effecting accesses keep their relative order and none is dropped.
; MIR-LABEL: name: order_writes
; MIR: SETBBIT @flag
; MIR-NEXT: CPLBIT @flag
; MIR-NEXT: CLRBIT @flag
; MIR-NOT: MOVCBIT
define void @order_writes() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  call void @llvm.mcs251.bit.obj.toggle(ptr @flag)
  call void @llvm.mcs251.bit.obj.clear(ptr @flag)
  ret void
}

; A read whose result is unused still performs the access: no dead-access
; elimination (section 1.2 -- the read may also write memory, it is not a
; pure function).
; MIR-LABEL: name: unused_read
; MIR: MOVCBIT @flag
; MIR-NOT: SETBBIT
define void @unused_read() {
  %b = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  ret void
}

; `if (B)` is exactly one JNB bit test (section 2.4): no materialised sample
; group, no byte compare, anywhere in the function.
; MIR-LABEL: name: cond_direct
; MIR: JNB {{%bb\.[0-9]+}}, @flag
; MIR-NOT: MOVCBIT
; MIR-NOT: CMP8ri
define void @cond_direct() {
  %b = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  br i1 %b, label %t, label %f
t:
  call void @llvm.mcs251.bit.obj.toggle(ptr @flag)
  br label %f
f:
  ret void
}

; The inverted `if (!B)` is one JB test with the opposite polarity -- also
; not a materialised sample plus compare.
; MIR-LABEL: name: cond_inverted
; MIR: JB {{%bb\.[0-9]+}}, @flag
; MIR-NOT: MOVCBIT
; MIR-NOT: CMP8ri
; ... and the MIR check scope ends here: the optimiser-facing functions below
; (whose unused samples are materialised by design) are outside every MIR
; assertion.
; MIR-LABEL: name: double_set
define void @cond_inverted() {
  %b = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  %nb = xor i1 %b, -1
  br i1 %nb, label %t, label %f
t:
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  br label %f
f:
  ret void
}

; The optimiser-facing functions below are outside every MIR check scope; the
; trailing volatile store keeps the shape stable under the pass pipelines.

; OPT-LABEL: define void @double_set
; Exactly two writes survive: the accesses may not be CSEd into one
; (section 1.2 -- no speculative merging of two dynamic accesses).
; OPT-COUNT-2: call void @llvm.mcs251.bit.obj.set
; OPT-LABEL: define void @two_unused_reads
; Both reads survive: no DCE, and no CSE of the two samples either.
; OPT-COUNT-2: call i1 @llvm.mcs251.bit.obj.read
; OPT-LABEL: define void @ordered
; The ordinary byte store/load stay around the controlled accesses, in
; order: the handle is identity, NOT a disjoint memory location (section
; 1.2), so the byte accesses cannot move across them and the load cannot be
; forwarded over the set.
; OPT: store i8 %v, ptr @byte
; OPT: call void @llvm.mcs251.bit.obj.set
; OPT: load i8, ptr @byte
; OPT: call void @llvm.mcs251.bit.obj.clear
define void @double_set() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  store volatile i8 0, ptr @byte
  ret void
}
define void @two_unused_reads() {
  %a = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  %b = call i1 @llvm.mcs251.bit.obj.read(ptr @flag)
  store volatile i8 0, ptr @byte
  ret void
}
define void @ordered(i8 %v) {
  store i8 %v, ptr @byte
  call void @llvm.mcs251.bit.obj.set(ptr @flag)
  %x = load i8, ptr @byte
  call void @llvm.mcs251.bit.obj.clear(ptr @flag)
  store volatile i8 %x, ptr @byte
  ret void
}

; Object checks. The defined handle's kind-1 record: version 1, kind 1,
; init 1 (the module's only record -- the extern carries none), caps 1, four
; zero bytes.
;ELF:      Name: .mcs251.bit
;ELF:      SectionData (
;ELF-NEXT:   0000: 01010101 00000000
; Every use is a zero-addend BITADDR8 naming the exact symbol (the extern
; too: a cross-TU use is a reference, not a record). The implicit-check-not
; filters pin that no _flag use ever degrades into an ordinary 16/24-bit
; address relocation and the extern never gains a record association.
;ELF:      R_MCS251_BITADDR8 _flag 0x0
;ELF:      R_MCS251_BITADDR8 _ext 0x0
; Exactly one zero-addend BIT_REF naming the defining symbol, at record + 4.
;ELF:      0x4 R_MCS251_BIT_REF _flag 0x0
; (The module's own ordinary @byte global legitimately owns its DSEG/XINIT
; storage; the HANDLE's no-ordinary-storage property is pinned by
; bit-object.ll's definition-only module.)

; L01: declaration/call signature. A wrong-signature declaration is rejected
; even when entirely unused. O0 and O2 agree; the verifier-on variant shows
; the generic verifier rejects the same input first, and that the target
; diagnostic is its own, separate layer.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/badret.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not --crash llc -mtriple=mcs251 -O2 -disable-verify -filetype=obj -mcs251-object-format=elf %t/badret.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/badparam.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/vararg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/defined.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/coldcc.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIG
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %t/badret.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIGV
;SIG: MCS251 contract violation: MCS251 symbolic bit intrinsic: invalid declaration or call signature
;SIGV: intrinsic return type expected i1, but got i8
;SIGV: input module cannot be verified

; L02: call form -- only a plain, direct, unbundled CallInst. The intrinsic's
; own identity may only ever be the direct callee of a whitelisted call, so
; exporting it to an initializer is rejected with the same body.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/invoke.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/tail.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
; RUN: not --crash llc -mtriple=mcs251 -O2 -disable-verify -filetype=obj -mcs251-object-format=elf %t/tail.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/musttail.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/bundle.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/addrtaken.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORM
;FORM: MCS251 contract violation: MCS251 symbolic bit intrinsic: only direct unbundled calls are supported

; Section 1.3 B / 1.4: the handle itself inside an ordinary call's operand
; bundle is the handle's own frozen body, not the intrinsic's.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/escapebundle.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=HANDLE
;HANDLE: MCS251 contract violation: MCS251 bit object 'flag': handle must not be used by a non-whitelisted call or operand bundle

; L03: the operand must dyn_cast DIRECTLY to a marked AS0 global. Null,
; undef, an ordinary global, a GEP (never stripped), a select or PHI of
; handles, an alloca, and a handle forwarded through a function parameter
; are all rejected.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/nullarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/undefarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/plainarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/geparg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/selectarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/phiarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/allocaarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/paramhandle.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
;OPERAND: MCS251 contract violation: MCS251 symbolic bit intrinsic: operand must be a direct AS0 bit-object global

; L04: an illegal use inside a constant-false branch is rejected BEFORE any
; optimisation can remove it -- at both opt levels.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/deadcode.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND
; RUN: not --crash llc -mtriple=mcs251 -O2 -disable-verify -filetype=obj -mcs251-object-format=elf %t/deadcode.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=OPERAND

; L07: effect narrowing. The worst-case effect model of section 1.2 may not
; be narrowed at the call site (the IR reader resets attribute groups on
; intrinsic DECLARATIONS, so the call site is the expressible layer), and the
; identity operand may not carry aliasing promises or metadata.
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/callmemattr.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECTS
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/noaliasarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECTS
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/derefarg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECTS
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/aliasmd.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECTS
; RUN: not --crash llc -mtriple=mcs251 -O0 -disable-verify -filetype=obj -mcs251-object-format=elf %t/noaliasmd.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECTS
;EFFECTS: MCS251 contract violation: MCS251 symbolic bit intrinsic: incompatible effects or pointer attributes

; L09: the symbolic bit-address field has no assembly-text representation at
; all (an extern-only TU included: a use without a record is still ELF-only),
; and the REL object path rejects the relocation just the same.
; RUN: not --crash llc -mtriple=mcs251 -O0 -filetype=asm %t/externuse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELFONLY
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj %t/externuse.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELFONLY
;ELFONLY: MCS251 bit object requires ELF object output

;--- badret.ll
target triple = "mcs251"
declare i8 @llvm.mcs251.bit.obj.read(ptr)
define void @f() { ret void }
;--- badparam.ll
target triple = "mcs251"
declare i1 @llvm.mcs251.bit.obj.read(i32)
define void @f() { ret void }
;--- vararg.ll
target triple = "mcs251"
declare i1 @llvm.mcs251.bit.obj.read(ptr, ...)
define void @f() { ret void }
;--- defined.ll
target triple = "mcs251"
define i1 @llvm.mcs251.bit.obj.read(ptr %p) {
entry:
  ret i1 false
}
;--- coldcc.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call coldcc void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
;--- invoke.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
entry:
  invoke void @llvm.mcs251.bit.obj.set(ptr @flag)
      to label %ok unwind label %bad
ok:
  ret void
bad:
  landingpad { ptr, i32 } cleanup
  ret void
}
;--- tail.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  tail call void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
;--- musttail.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  musttail call void @llvm.mcs251.bit.obj.set(ptr @flag)
  ret void
}
;--- bundle.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag) [ "deopt"(ptr @flag) ]
  ret void
}
;--- addrtaken.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
@fp = global ptr addrspace(4) @llvm.mcs251.bit.obj.set
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  ret void
}
;--- escapebundle.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @g()
define void @f() {
  call void @g() [ "deopt"(ptr @flag) ]
  ret void
}
;--- nullarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr null)
  ret void
}
;--- undefarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr undef)
  ret void
}
;--- plainarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
@plain = global i8 5
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr @plain)
  ret void
}
;--- geparg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr getelementptr (i8, ptr @flag, i16 1))
  ret void
}
;--- selectarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f(i1 %c) {
  %p = select i1 %c, ptr @flag, ptr @flag
  call void @llvm.mcs251.bit.obj.set(ptr %p)
  ret void
}
;--- phiarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %m
b:
  br label %m
m:
  %p = phi ptr [ @flag, %a ], [ @flag, %b ]
  call void @llvm.mcs251.bit.obj.set(ptr %p)
  ret void
}
;--- allocaarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  %a = alloca i8
  call void @llvm.mcs251.bit.obj.set(ptr %a)
  ret void
}
;--- paramhandle.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @g(ptr %p) {
  call void @llvm.mcs251.bit.obj.set(ptr %p)
  ret void
}
define void @f() {
  call void @g(ptr @flag)
  ret void
}
;--- deadcode.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @live() {
  br i1 false, label %dead, label %ok
dead:
  call void @llvm.mcs251.bit.obj.set(ptr null)
  br label %ok
ok:
  ret void
}
;--- callmemattr.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag) #1
  ret void
}
attributes #1 = { memory(argmem: read) }
;--- noaliasarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr noalias @flag)
  ret void
}
;--- derefarg.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr dereferenceable(1) @flag)
  ret void
}
;--- aliasmd.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag), !alias.scope !0
  ret void
}
!0 = !{!1}
!1 = distinct !{!1, !"some scope"}
;--- noaliasmd.ll
target triple = "mcs251"
@flag = global i8 0 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.set(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.set(ptr @flag), !noalias !0
  ret void
}
!0 = !{!1}
!1 = distinct !{!1, !"some scope"}
;--- externuse.ll
target triple = "mcs251"
@ext = external global i8 #0
attributes #0 = { "mcs251-bit-object" }
declare void @llvm.mcs251.bit.obj.clear(ptr)
define void @f() {
  call void @llvm.mcs251.bit.obj.clear(ptr @ext)
  ret void
}
