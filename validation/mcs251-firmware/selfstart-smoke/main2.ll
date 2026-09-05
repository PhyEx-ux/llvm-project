; main2.ll - selfstart-smoke case 2: LLVM _main prints 'M', arms T0
; (mode1, full 65536-tick countdown, EA|ET0) and returns WITHOUT linking
; any TF0 handler.  The default-vector table in crt-selfstart.asm must
; catch the overflow: isr_unhandled prints '!'.  Expected transcript: MS!
; (M = main ran, S = main returned, ! = default TF0 slot dispatched)
define void @_main() {
entry:
  store volatile i8 77, ptr inttoptr(i32 153 to ptr)   ; 'M' -> SBUF 0x99
  store volatile i8 1, ptr inttoptr(i32 137 to ptr)    ; TMOD 0x89 = 0x01
  store volatile i8 0, ptr inttoptr(i32 140 to ptr)    ; TH0  0x8C = 0x00
  store volatile i8 0, ptr inttoptr(i32 138 to ptr)    ; TL0  0x8A = 0x00
  store volatile i8 130, ptr inttoptr(i32 168 to ptr)  ; IE   0xA8 = 0x82 (EA|ET0)
  %t = load volatile i8, ptr inttoptr(i32 136 to ptr)  ; TCON 0x88
  %t2 = or i8 %t, 16                                   ; TR0 = 1
  store volatile i8 %t2, ptr inttoptr(i32 136 to ptr)
  ret void
}
