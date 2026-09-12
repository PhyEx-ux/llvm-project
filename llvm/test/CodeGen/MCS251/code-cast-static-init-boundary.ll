; RUN: not --crash llc -mtriple=mcs251 -mcs251-memory-contract=1,1,32,8,1 -O0 \
; RUN:   -mcs251-object-format=elf -filetype=obj < %s 2>&1 | FileCheck %s
;
; CP-A stage boundary (RUNTIME-AS-PTR-DESIGN-A.md §3-A5, DESIGN.md CP-A
; "落地时机"): the runtime conversion A3 opens is NOT a static initializer
; protocol. A new static pointer initializer containing an addrspacecast stays
; fail-closed until the separate CP-A relocation is approved and implemented,
; and the X3 leaf whitelist must NOT be widened to strip the cast.
;
; This pins the *boundary*: the module reaches llc (the frontend accepts the
; expression form), and the object writer refuses it rather than silently
; emitting a guessed relocation.

@rom = addrspace(4) constant [4 x i8] c"\01\02\03\04", align 1
@p = global ptr addrspacecast (ptr addrspace(4) @rom to ptr), align 1

; CHECK: MCS251: defined global data requires byte-aligned default-address-space i8/i16/i32 scalar, array, or struct storage
