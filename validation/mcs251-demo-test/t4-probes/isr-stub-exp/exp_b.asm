; exp_b.asm - T4 ISR stub form experiment, Form B: vector slot at 0xFF000B
; contains `ecall _isr_body; reti`.  ECALL pushes a 24-bit return address,
; matching the LLVM function's ERET (24-bit pop); RETI then pops the
; hardware interrupt frame.  Everything else identical to exp_a.
;
; Banner 'vB'.
        .module exp_b
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
TMOD    = 0x89
TL0     = 0x8a
TH0     = 0x8c
IE      = 0xa8
FLAG    = 0x30
CNT     = 0x31

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

        .area VEC (CODE)
        ecall   _isr_body               ; FORM B: 24-bit call matches ERET
        reti

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
        mov     a,#'v'
        ecall   putc
        mov     a,#'B'
        ecall   putc

        mov     FLAG,#0
        mov     CNT,#0
        mov     TMOD,#0x01
        mov     TH0,#0xff
        mov     TL0,#0xf0
        mov     IE,#0x82                ; EA|ET0
        orl     TCON,#0x10              ; TR0=1

        mov     r5,#0x40
w1:     mov     r6,#0
w2:     mov     r7,#0
w3:     mov     a,FLAG
        jnz     got
        djnz    r7,w3
        djnz    r6,w2
        djnz    r5,w1
        ejmp    fail
got:
        anl     TCON,#0xef              ; TR0=0
        mov     a,CNT
        ecall   pa
        mov     a,CNT
        jz      failj
        mov     a,TCON
        ecall   pa
        mov     a,TCON
        anl     a,#0x20
        jnz     failj
        mov     a,#'K'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

        .globl  _isr_body

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
