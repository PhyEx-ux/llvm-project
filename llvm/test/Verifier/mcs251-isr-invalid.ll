; RUN: split-file %s %t
; RUN: not llvm-as %t/missing.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
; RUN: not llvm-as %t/reserved.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/call.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/address.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/escape-before-used.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/used-before-escape.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/alias.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/compiler-used.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/used-nosection.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/used-nonappending.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/used-nonpointer.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=KEPT
; RUN: not llvm-as %t/callcc128-ordinary.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/callcc128-indirect.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/invokecc128-ordinary.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/callbrcc128-asm.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: not llvm-as %t/ifunc.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/blockaddress-global.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; RUN: not llvm-as %t/blockaddress-inst.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=USE
; G1-1 boundary matrix: the profile is 0-126. Everything past the upper
; bound (127, the rejected 133 alternative, 255, u32 max, negative, hex
; text) and the reserved/system/header-only spots are rejected with the
; shared-constant message; the new upper bound and the reclassified slots
; 31/45/46 verify clean.
; RUN: not llvm-as %t/oob127.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/oob133.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/oob255.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/oob-u32max.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/oobneg.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/hextext.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/sys14.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/reserved13.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: not llvm-as %t/headeronly81.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLOT
; RUN: llvm-as %t/high126-ok.ll -o /dev/null
; RUN: llvm-as %t/reclass-ok.ll -o /dev/null
; RUN: llvm-as %t/blockaddress-ordinary-ok.ll -o /dev/null
; RUN: llvm-as %t/as4-direct-ok.ll -o /dev/null
; RUN: llvm-as %t/as4-cast-ok.ll -o /dev/null

; MISSING: MCS251 ISR: calling convention and vector attribute must appear together
; SLOT: MCS251 ISR: vector is not a legal slot in profile 0-126
; CALL: MCS251 ISR: interrupt entry may not be called
; USE: MCS251 ISR: interrupt entry has a non-registration use
; KEPT: MCS251 ISR: interrupt definition must be kept alive by llvm.used

;--- missing.ll
declare cc 128 void @irq()

;--- reserved.ll
declare cc 128 void @irq() "mcs251-isr-vector"="7"

;--- call.ll
declare cc 128 void @irq() "mcs251-isr-vector"="1"
define void @caller() {
  call cc 128 void @irq()
  ret void
}

;--- address.ll
@escaped = global ptr @irq
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; Shared-constant escape: the two identical [1 x ptr] initializers are one
; uniqued constant used by both @escaped and @llvm.used, so the verdict must
; not depend on the declaration (use-list) order. Both orders are rejected.
;--- escape-before-used.ll
@escaped = global [1 x ptr] [ptr @irq]
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
declare cc 128 void @irq() "mcs251-isr-vector"="1"

;--- used-before-escape.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
@escaped = global [1 x ptr] [ptr @irq]
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; An alias is not permitted as an intermediate of a registration structure.
;--- alias.ll
@llvm.used = appending global [1 x ptr] [ptr @a], section "llvm.metadata"
@a = alias void(), ptr @irq
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; One shared array feeding both llvm.used and llvm.compiler.used: the
; compiler.used branch is not a registration root.
;--- compiler-used.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; The keepalive container must carry the "llvm.metadata" section.
;--- used-nosection.ll
@llvm.used = appending global [1 x ptr] [ptr @irq]
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; The keepalive container must have appending linkage.
;--- used-nonappending.ll
@llvm.used = global [1 x ptr] [ptr @irq], section "llvm.metadata"
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; A root that is not an array of pointers is not a keepalive container, so
; the definition is unrooted.
;--- used-nonpointer.ll
@llvm.used = appending global [1 x i32] [i32 42], section "llvm.metadata"
define mcs251_intrcc void @irq() #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

; A call site that itself carries CC 128 is rejected even when the target is
; an ordinary function or an indirect callee.
;--- callcc128-ordinary.ll
declare void @helper()
define void @caller() {
  call cc 128 void @helper()
  ret void
}

;--- callcc128-indirect.ll
define void @caller(ptr %p) {
  call cc 128 void %p()
  ret void
}

;--- invokecc128-ordinary.ll
declare void @helper()
declare i32 @__gxx_personality_v0(...)
define void @caller() personality ptr @__gxx_personality_v0 {
  invoke cc 128 void @helper() to label %ok unwind label %bad
ok:
  ret void
bad:
  landingpad { ptr, i32 } cleanup
  ret void
}

;--- callbrcc128-asm.ll
define void @caller() {
entry:
  callbr cc 128 void asm "", "!i"()
    to label %fallthrough [label %indirect]
fallthrough:
  ret void
indirect:
  ret void
}

; An ifunc whose resolver is the entry is an ordinary escape.
;--- ifunc.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
@i = ifunc void(), ptr @irq
declare cc 128 void @irq() "mcs251-isr-vector"="1"

; BlockAddress constants carry no operands, so they bypass the function use
; graph; every real use of an ISR block address is still an ordinary escape.
;--- blockaddress-global.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
@e = global ptr blockaddress(@irq, %body)
define cc 128 void @irq() noinline "mcs251-isr-vector"="1" {
entry:
  br label %body
body:
  ret void
}

;--- blockaddress-inst.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
define ptr @user() {
entry:
  ret ptr blockaddress(@irq, %body)
}
define cc 128 void @irq() noinline "mcs251-isr-vector"="1" {
entry:
  br label %body
body:
  ret void
}

; Ordinary functions keep taking block addresses; the ISR rules must not
; affect them.
;--- blockaddress-ordinary-ok.ll
@dest = global ptr blockaddress(@user, %bb)
define ptr @user() {
entry:
  ret ptr blockaddress(@user, %bb)
bb:
  ret ptr null
}

; Positives: an AS4 direct used member and the standard address-space
; adaptation cast both remain legal keepalive roots.
;--- as4-direct-ok.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr addrspace(4)] [ptr addrspace(4) @irq], section "llvm.metadata"
define mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- as4-cast-ok.ll
target triple = "mcs251-unknown-none"
@llvm.used = appending global [1 x ptr] [ptr addrspacecast (ptr addrspace(4) @irq to ptr)], section "llvm.metadata"
define mcs251_intrcc void @irq() addrspace(4) #0 {
  ret void
}
attributes #0 = { noinline "mcs251-isr-vector"="1" }

;--- oob127.ll
declare cc 128 void @irq() "mcs251-isr-vector"="127"

;--- oob133.ll
declare cc 128 void @irq() "mcs251-isr-vector"="133"

;--- oob255.ll
declare cc 128 void @irq() "mcs251-isr-vector"="255"

; u32 maximum: the canonical parser rejects anything past the profile bound,
; not just values that fit a slot field.
;--- oob-u32max.ll
declare cc 128 void @irq() "mcs251-isr-vector"="4294967295"

;--- oobneg.ll
declare cc 128 void @irq() "mcs251-isr-vector"="-1"

; The canonical parser is decimal-only; hex spellings are not legal IR.
;--- hextext.ll
declare cc 128 void @irq() "mcs251-isr-vector"="0x7E"

;--- sys14.ll
declare cc 128 void @irq() "mcs251-isr-vector"="14"

;--- reserved13.ll
declare cc 128 void @irq() "mcs251-isr-vector"="13"

; HeaderOnly evidence (no manual row) stays Reserved pending further proof.
;--- headeronly81.ll
declare cc 128 void @irq() "mcs251-isr-vector"="81"

; The new upper bound verifies clean end to end.
;--- high126-ok.ll
@llvm.used = appending global [1 x ptr] [ptr @irq], section "llvm.metadata"
define cc 128 void @irq() noinline "mcs251-isr-vector"="126" {
  ret void
}

; D3 reclassification: 31/45/46 are Legal under the G1 profile.
;--- reclass-ok.ll
@llvm.used = appending global [3 x ptr] [ptr @a, ptr @b, ptr @c], section "llvm.metadata"
define cc 128 void @a() noinline "mcs251-isr-vector"="31" {
  ret void
}
define cc 128 void @b() noinline "mcs251-isr-vector"="45" {
  ret void
}
define cc 128 void @c() noinline "mcs251-isr-vector"="46" {
  ret void
}
