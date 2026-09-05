; crt-selfstart.asm - Step 4a self-hosted startup module (no SDCC assets).
;
; Replaces the SDCC-harness-provided startup glue (HOME reset trampoline,
; GSFINAL tail jump, __sdcc_program_startup) and crt0.asm (GSINIT0) for
; pure asm+LLVM images.  Design and evidence: STEP4-SELFSTART-DESIGN.md.
;
; Layout (bases fixed by link-selfstart.lk):
;   HOME 0xff0000  ljmp boot                 3 bytes; INT0 slot stays free
;   VECS 0xff0003  8 x { ejmp isr_unhandled; .ds 4 }   64 bytes; covers
;                  INT0/TF0/INT1/TF1/UART1/ADC/LVD/PCA at 0xFF0003+n*8
;   BOOT 0xff0100  mov spx,#0x2fff; ecall _main; marker 'S'; spin
;
; Serial markers this module may emit (both indicate protocol-relevant
; events, neither ever appears in a well-formed PASS transcript):
;   'S'  _main returned to boot (current harness protocol: main does NOT
;        return; a visible 'S' means the module under test came back)
;   '!'  an interrupt fired with no handler linked (default-vector net)
;
; Interrupt-using images must NOT link this module's VECS area: ASxxxx has
; no weak symbols, so per-test vector modules (see
; validation/mcs251-demo-test/t4/timer0-irq-llvm/vector-stub.asm) replace
; the default net wholesale.  crt-selfstart.asm without VECS is not a
; supported configuration (the 8-slot table is the asset's safety value).
;
; The .optsdcc line is part of the ASxxxx/SDLD ABI contract; keep it byte
; for byte in sync with crt0.asm and the -A line of link-selfstart.lk.
        .module mcs251_crt_selfstart
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99

        .globl  __mcs251_selfstart_boot
        .globl  __mcs251_isr_unhandled

        .area HOME (CODE)
__mcs251_reset:
        ljmp    __mcs251_selfstart_boot ; 3 bytes: 02 hh ll, bank-preserving

        .area VECS (CODE)
        ejmp    __mcs251_isr_unhandled  ; INT0  0xff0003
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; TF0   0xff000b
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; INT1  0xff0013
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; TF1   0xff001b
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; UART1 0xff0023
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; ADC   0xff002b
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; LVD   0xff0033
        .ds     4
        ejmp    __mcs251_isr_unhandled  ; PCA   0xff003b
        .ds     4

        .area BOOT (CODE)
__mcs251_selfstart_boot::
        mov     spx,#0x2fff             ; same stack top as crt0.asm
        ecall   _main                   ; 24-bit call, matches LLVM ERET
        mov     SBUF,#'S'               ; main returned (protocol marker)
__mcs251_halt:
        sjmp    __mcs251_halt

__mcs251_isr_unhandled::
        mov     SBUF,#'!'               ; unexpected interrupt, no handler
__mcs251_ispin:
        sjmp    __mcs251_ispin

        .globl  _main                   ; supplied by the module under test
