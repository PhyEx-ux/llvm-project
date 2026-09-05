; vector-stub.asm - T4 timer0-irq-llvm: asm vector stub + main harness for
; the "ordinary LLVM function as ISR body" test.
;
; Form per t4-probes/ISR-STUB-VERDICT.md: the plain `lcall _isr_body; reti`
; hypothesis FAILS in QEMU (LCALL pushes 16-bit, the LLVM function's ERET
; pops 24-bit -> runaway PC).  The working minimal form is
; `ecall _isr_body; reti`; this test uses the hardened delivery form:
;
;   VEC (0xff000b):  ejmp isr_stub
;   isr_stub:        push PSW/ACC/B/R0-R7; ecall _isr_body; pop ...; reti
;
; so the LLVM body may clobber any classic register without corrupting main.
;
; Main harness (d1_tint-derived, probe idiom):
;   phase 1: T0 mode1, TH0:TL0=0xFFF0 (16 ticks), EA|ET0, poll FLAG with
;            timeout; require CNT>=1, TF0 auto-cleared on entry, r0 sentinel
;            0x5A intact (proves the stub's context save works).
;   phase 2: clear CNT/FLAG, re-arm, require CNT==1 and TF0 clear again.
;            Proves repeated entry/exit keeps the stack balanced.
;   The LLVM ISR body itself clears TR0 (TCON &= ~0x10), so every arming
;   produces exactly one fire: QEMU re-fires TF0 quickly while TR0 stays
;   up, which would otherwise make the entry count (and the transcript)
;   timing-dependent.
;
; Transcript: T4 I <CNT> <TCON> K I 01 00 K PASS   == "T4I0100KI0100K\nPASS"
;
; Areas (linked by link.lk via mcs251_ld.py):
;   HOME  0xff0000  reset stub (7 bytes; TF0 vector at 0xff000b stays free)
;   VEC   0xff000b  TF0 vector: ejmp isr_stub
;   PROBE 0xff0100  main + utilities + isr_stub
;   CSEG  0xff0200  LLVM module (_isr_body)
        .module vector_stub
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
TMOD    = 0x89
TL0     = 0x8a
TH0     = 0x8c
IE      = 0xa8
PSW     = 0xd0
ACC     = 0xe0
B       = 0xf0
FLAG    = 0x30
CNT     = 0x31

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

        .area VEC (CODE)
        ejmp    isr_stub                ; TF0 vector -> hardened stub

        .area PROBE (CODE)

putc:
        mov     SBUF,a
        eret

pn:
        mov     r1,a
        clr     c
        subb    a,#10
        jc      pn_low
        mov     a,r1
        add     a,#('A'-10)
        ecall   putc
        eret
pn_low:
        mov     a,r1
        add     a,#'0'
        ecall   putc
        eret

pa:
        mov     r2,a
        swap    a
        anl     a,#0x0f
        ecall   pn
        mov     a,r2
        anl     a,#0x0f
        ecall   pn
        eret

pass:
        mov     a,#0x0a
        ecall   putc
        mov     a,#'P'
        ecall   putc
        mov     a,#'A'
        ecall   putc
        mov     a,#'S'
        ecall   putc
        ecall   putc
        mov     a,#0x0a
        ecall   putc
pspin:  ejmp    pspin

fail:
        mov     a,#0x0a
        ecall   putc
        mov     a,#'F'
        ecall   putc
        mov     a,#'A'
        ecall   putc
        mov     a,#'I'
        ecall   putc
        mov     a,#'L'
        ecall   putc
        mov     a,#0x0a
        ecall   putc
fspin:  ejmp    fspin

main:
        mov     a,#'T'
        ecall   putc
        mov     a,#'4'
        ecall   putc

        mov     FLAG,#0
        mov     CNT,#0
        mov     r0,#0x5a                ; sentinel: must survive every IRQ
        mov     TMOD,#0x01              ; T0 mode1 16-bit
        mov     TH0,#0xff
        mov     TL0,#0xf0               ; phase 1: 16 ticks to overflow
        mov     IE,#0x82                ; EA|ET0
        orl     TCON,#0x10              ; TR0=1

        ; poll FLAG with timeout
        mov     r5,#0x40
w1:     mov     r6,#0
w2:     mov     r7,#0
w3:     mov     a,FLAG
        jnz     got1
        djnz    r7,w3
        djnz    r6,w2
        djnz    r5,w1
        ejmp    fail                    ; interrupt never fired
got1:
        anl     TCON,#0xef              ; TR0=0
        cjne    r0,#0x5a,failj          ; stub must have preserved r0
        mov     a,CNT
        ecall   pa                      ; evidence: ISR entry count (>=1)
        mov     a,CNT
        jz      failj
        mov     a,TCON
        ecall   pa                      ; evidence: TCON after ISR
        mov     a,TCON
        anl     a,#0x20
        jnz     failj                   ; TF0 still set -> no auto-clear
        mov     a,#'K'
        ecall   putc

        ; ---- phase 2: second, exactly-one fire ----
        mov     FLAG,#0
        mov     CNT,#0
        mov     TH0,#0xff
        mov     TL0,#0x00               ; any reload; the ISR stops T0 itself
        orl     TCON,#0x10              ; TR0=1
        mov     r5,#0x40
v1:     mov     r6,#0
v2:     mov     r7,#0
v3:     mov     a,FLAG
        jnz     got2
        djnz    r7,v3
        djnz    r6,v2
        djnz    r5,v1
        ejmp    fail
got2:
        anl     TCON,#0xef              ; TR0=0
        cjne    r0,#0x5a,failj
        mov     a,CNT
        ecall   pa                      ; evidence: must read 01 exactly
        mov     a,CNT
        xrl     a,#0x01
        jnz     failj                   ; not exactly one fire
        mov     a,TCON
        ecall   pa
        mov     a,TCON
        anl     a,#0x20
        jnz     failj
        mov     a,#'K'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

; ---- hardened vector stub: full classic context around the LLVM body ----
; (r8-r15 are outside the classic bank-0 set; extend here if the backend
;  ever emits them for an ISR body.)
isr_stub:
        push    PSW
        push    ACC
        push    B
        push    0x00                    ; r0
        push    0x01                    ; r1
        push    0x02
        push    0x03
        push    0x04
        push    0x05
        push    0x06
        push    0x07
        ecall   _isr_body               ; 24-bit call, matches ERET
        pop     0x07
        pop     0x06
        pop     0x05
        pop     0x04
        pop     0x03
        pop     0x02
        pop     0x01
        pop     0x00
        pop     B
        pop     ACC
        pop     PSW
        reti

        .globl  _isr_body               ; defined by isr-body.ll via llc

; ---- linker driver stubs (harmless for mcs251_ld.py, needed if relinked
;      through the sdcc driver) ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
