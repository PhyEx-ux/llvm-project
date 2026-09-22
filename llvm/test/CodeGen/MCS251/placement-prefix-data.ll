; G11-B review [建议] (2026-09-16, probe /tmp/g11b-review-alice/prefix.ll): a
; fixed function with IR prefix data is REJECTED fail-closed. The base
; emitFunctionHeader emits prefix bytes BEFORE the entry label, while the
; stream is still in the ordinary text section -- the four bytes would live
; outside the entity's fixed section, outside NOTE.size/sh_size and outside
; the defined symbol's span. Instead of "placing" bytes the contract has no
; ruling for, the emitter keeps the entry-label geometry claim true by
; construction (the comment above emitFunctionEntryLabel is scoped to
; inputs without prefix/prologue data). Prologue data is rejected for the
; same reason family (emitter-fed bytes with no placement ruling).
;
; RUN: not llc -mtriple=mcs251 -O0 -filetype=obj -mcs251-object-format=elf %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PFX
; PFX: LLVM ERROR: MCS251: fixed function 'fixed': prefix/prologue data is not supported on a fixed placement function

target triple = "mcs251-unknown-none"

define void @fixed() addrspace(4) #0 prefix i32 123 { ret void }

!mcs251.signatures = !{!0}
!0 = !{!"_fixed", i32 1, i32 0, i32 0}

attributes #0 = { "mcs251-place"="0xFC3000,code,function,owned,0" "mcs251-stable-symbol"="fixed" }
