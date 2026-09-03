; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; The caller-side mirror of args-error-multi.ll: the SDCC mcs251 ABI passes a
; second argument in a static OSEG overlay slot (_FUNCNAME_PARM_n), which this
; backend does not implement, so calling a two-argument callee is rejected
; with a clear error instead of silently misplacing an argument. (The
; constants keep the caller itself within the single-argument restriction so
; the error is really raised by the call lowering.)
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

declare i16 @g2(i16 %a, i16 %b)

define i16 @call_two() {
; CHECK: LLVM ERROR: MCS251 multi-argument calls need SDCC OSEG overlay slots (not yet supported)
  %r = call i16 @g2(i16 1, i16 2)
  ret i16 %r
}
