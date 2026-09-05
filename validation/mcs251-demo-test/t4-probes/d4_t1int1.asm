; Probe D4: Timer1 overflow interrupt + INT1 external interrupt.
;
; Vectors: INT1 -> 0xFF0013, TF1 -> 0xFF001B.
;   D4a  TF1: T1 mode1, EA|ET1, overflow vectors to 0xFF001B; ISR prints 'J',
;        TF1 auto-cleared on entry
;   D4b  INT1: P3.3 push-pull falling edge with IT1=1, EA|EX1, vectors to
;        0xFF0013; ISR prints 'F', IE1 auto-cleared on entry
;
        .module probe_d4_t1int1
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
TMOD    = 0x89
TL1     = 0x8b
TH1     = 0x8d
IE      = 0xa8
P3      = 0xb0
P3M1    = 0xb1
P3M0    = 0xb2
FLAG1   = 0x33
FLAG2   = 0x34

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

        .area VEC1 (CODE)
        ejmp    isr_x1                  ; linked at 0xff0013

        .area VEC2 (CODE)
        ejmp    isr_t1                  ; linked at 0xff001b

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
        mov     a,#'d'
        ecall   putc
        mov     a,#'4'
        ecall   putc

        ; D4a: TF1 interrupt
        mov     FLAG1,#0
        mov     TMOD,#0x10              ; T1 mode1
        mov     TH1,#0xff
        mov     TL1,#0xf0
        mov     IE,#0x88                ; EA|ET1
        orl     TCON,#0x40              ; TR1=1
        mov     r5,#0x40
w1:     mov     r6,#0
w2:     mov     r7,#0
w3:     mov     a,FLAG1
        jnz     got1
        djnz    r7,w3
        djnz    r6,w2
        djnz    r5,w1
        ejmp    fail
got1:
        anl     TCON,#0xbf              ; TR1=0
        mov     IE,#0x00
        mov     a,TCON
        ecall   pa                      ; evidence: TCON after ISR (TF1=0)
        mov     a,TCON
        anl     a,#0x80
        jnz     failj                   ; TF1 must be auto-cleared
        mov     a,#'7'
        ecall   putc

        ; D4b: INT1 via P3.3 falling edge
        mov     FLAG2,#0
        anl     P3M1,#0xf7              ; P3.3 push-pull
        orl     P3M0,#0x08
        orl     P3,#0x08                ; pin high
        orl     TCON,#0x04              ; IT1=1 (edge)
        mov     IE,#0x84                ; EA|EX1
        anl     P3,#0xf7                ; falling edge -> IE1 -> INT1
        mov     r5,#0x40
v1:     mov     r6,#0
v2:     mov     r7,#0
v3:     mov     a,FLAG2
        jnz     got2
        djnz    r7,v3
        djnz    r6,v2
        djnz    r5,v1
        ejmp    fail
got2:
        mov     IE,#0x00
        mov     a,TCON
        ecall   pa                      ; evidence: IE1 cleared (bit3)
        mov     a,TCON
        anl     a,#0x08
        jnz     failj
        mov     a,#'8'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

; ---- ISRs ------------------------------------------------------------
isr_t1:
        mov     SBUF,#'J'
        mov     FLAG1,#1
        reti

isr_x1:
        mov     SBUF,#'F'
        mov     FLAG2,#1
        reti

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
