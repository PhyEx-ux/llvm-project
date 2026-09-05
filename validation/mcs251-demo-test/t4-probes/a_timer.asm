; Probe A: STC32G Timer0/Timer1 behavior under QEMU stc32g144k246.
;
; Checks (each prints a marker letter on success, hex evidence where useful):
;   A1  AUXR reset value (expect 0x01)
;   A2  TMOD latches written value
;   A3  T0 mode1: counter advances and overflow sets TF0 (polled)
;   A4  TR0=0: TH0:TL0 stable across a delay
;   A5  T0 mode2: 8-bit auto-reload from TH0 (TL0 stays in [TH0,0xFF])
;   A6  T1 mode1: overflow sets TF1 (polled)
;   A7  XFR TM0PS (0x7efea0, EAXFR window) latches written value
;
; Output contract: banner 'a', per-check markers '1'..'7', hex bytes,
; then PASS (spin) or FAIL (spin).  Serial transcript is the only oracle.

        .module probe_a_timer
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
TMOD    = 0x89
TL0     = 0x8a
TL1     = 0x8b
TH0     = 0x8c
TH1     = 0x8d
AUXR    = 0x8e
P_SW2   = 0xba

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

; ---- utilities ------------------------------------------------------

; putc: print acc via UART1 SBUF.  Clobbers nothing.
putc:
        mov     SBUF,a
        eret

; pn: print low nibble of acc as one hex char.  Clobbers a, r1.
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

; pa: print acc as two hex chars.  Clobbers a, r1, r2.
pa:
        mov     r2,a
        swap    a
        anl     a,#0x0f
        ecall   pn
        mov     a,r2
        anl     a,#0x0f
        ecall   pn
        eret

; delay: ~0.5M djnz iterations.  Clobbers r5,r6,r7.
delay:
        mov     r5,#0x08
d1:     mov     r6,#0
d2:     mov     r7,#0
d3:     djnz    r7,d3
        djnz    r6,d2
        djnz    r5,d1
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

; ---- probe body -----------------------------------------------------

main:
        mov     a,#'a'
        ecall   putc

        ; A1: AUXR reset value
        mov     a,AUXR
        ecall   pa
        mov     a,AUXR
        xrl     a,#0x01
        jz      a1ok
        ejmp    fail
a1ok:   mov     a,#'1'
        ecall   putc

        ; A2: TMOD latch
        mov     TMOD,#0x01
        mov     a,TMOD
        ecall   pa
        mov     a,TMOD
        xrl     a,#0x01
        jz      a2ok
        ejmp    fail
a2ok:   mov     a,#'2'
        ecall   putc

        ; A3: T0 mode1, 16 ticks to overflow, poll TF0 with timeout
        mov     TH0,#0xff
        mov     TL0,#0xf0
        orl     TCON,#0x10              ; TR0=1
        mov     r5,#0x40
a3p1:   mov     r6,#0
a3p2:   mov     r7,#0
a3p3:   mov     a,TCON
        anl     a,#0x20                 ; TF0
        jnz     a3ok
        djnz    r7,a3p3
        djnz    r6,a3p2
        djnz    r5,a3p1
        ejmp    fail                    ; timeout: TF0 never set
a3ok:   mov     a,#'3'
        ecall   putc

        ; A4: stop T0; TH0:TL0 must stay constant across a delay
        anl     TCON,#0xef              ; TR0=0
        anl     TCON,#0xdf              ; TF0=0
        mov     a,TH0
        mov     r3,a
        mov     a,TL0
        mov     r4,a
        ecall   delay
        mov     a,TH0
        xrl     a,r3
        jz      a4a
        ejmp    fail
a4a:    mov     a,TL0
        xrl     a,r4
        jz      a4ok
        ejmp    fail
a4ok:   mov     a,#'4'
        ecall   putc

        ; A5: mode2 8-bit auto-reload; after overflow TL0 in [0xF0,0xFF]
        mov     TMOD,#0x02
        mov     TH0,#0xf0               ; reload value
        mov     TL0,#0xfe               ; 2 ticks to overflow
        orl     TCON,#0x10              ; TR0=1
        mov     r5,#0x40
a5p1:   mov     r6,#0
a5p2:   mov     r7,#0
a5p3:   mov     a,TCON
        anl     a,#0x20
        jnz     a5got
        djnz    r7,a5p3
        djnz    r6,a5p2
        djnz    r5,a5p1
        ejmp    fail
a5got:  anl     TCON,#0xef              ; TR0=0
        anl     TCON,#0xdf              ; TF0=0
        mov     a,TL0
        mov     r3,a
        anl     a,#0xf0
        xrl     a,#0xf0
        jz      a5ev
        ejmp    fail                    ; TL0 outside reload window
a5ev:   mov     a,r3
        ecall   pa                      ; evidence: reloaded TL0
        mov     a,#'5'
        ecall   putc

        ; A6: T1 mode1 overflow sets TF1
        mov     TMOD,#0x10
        mov     TH1,#0xff
        mov     TL1,#0xf0
        orl     TCON,#0x40              ; TR1=1
        mov     r5,#0x40
a6p1:   mov     r6,#0
a6p2:   mov     r7,#0
a6p3:   mov     a,TCON
        anl     a,#0x80                 ; TF1
        jnz     a6ok
        djnz    r7,a6p3
        djnz    r6,a6p2
        djnz    r5,a6p1
        ejmp    fail
a6ok:   anl     TCON,#0xbf              ; TR1=0
        anl     TCON,#0x7f              ; TF1=0
        mov     a,#'6'
        ecall   putc

        ; A7: TM0PS XFR latch at 0x7efea0 (needs P_SW2.EAXFR=1)
        mov     P_SW2,#0x80
        mov     dptr,#0xfea0
        mov     dpxl,#0x7e
        mov     a,#0xa5
        mov     @dpx,a
        mov     a,@dpx
        ecall   pa
        mov     a,@dpx
        xrl     a,#0xa5
        jz      a7ok
        ejmp    fail
a7ok:   mov     a,#'7'
        ecall   putc
        clr     a                       ; restore TM0PS=0
        mov     @dpx,a

        ejmp    pass

; ---- sdld driver stubs: the mcs251 link script references these areas ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
