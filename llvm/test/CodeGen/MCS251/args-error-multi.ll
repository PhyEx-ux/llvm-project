; RUN: not --crash llc -mtriple=mcs251 < %s 2>&1 | FileCheck %s

; The SDCC mcs251 ABI passes the second and later arguments in static OSEG
; overlay slots (_FUNCNAME_PARM_n), which this minimal backend does not
; implement. Anything beyond the single dpl/dptr argument slot must fail
; with a clear error instead of being silently misplaced.
; (report_fatal_error aborts, hence --crash; lit pipelines are pipefail.)

define i16 @addw(i16 %a, i16 %b) {
; CHECK: LLVM ERROR: minimal MCS251 backend only supports zero or one i8/i16/i32 argument; SDCC multi-arg ABI uses static OSEG overlay slots (not yet supported)
  %t = add i16 %a, %b
  ret i16 %t
}
