; main.ll - selfstart-smoke case 1: LLVM _main prints 'M' and returns.
; Validates: reset entry, SPX init, boot->main ECALL/ERET round trip,
; post-main 'S' marker.  Expected transcript: MS
define void @main() {
entry:
  store volatile i8 77, ptr inttoptr(i32 153 to ptr)   ; 'M' -> SBUF 0x99
  ret void
}
