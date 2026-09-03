; Minimal input supported by the initial LLVM MCS-251 backend.
target triple = "mcs251-unknown-none"

; SDCC's MCS-251 ABI prefixes external C symbols with an underscore.
define void @_mcs251_probe() {
entry:
  ret void
}
