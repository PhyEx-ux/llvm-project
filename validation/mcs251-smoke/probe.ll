; Minimal input supported by the initial LLVM MCS-251 backend.
target triple = "mcs251-unknown-none"

; The backend (post-9d74d7ede ASxxxx m:s naming) decorates external C
; symbols with the leading underscore itself; IR must use the bare name.
define void @mcs251_probe() {
entry:
  ret void
}
