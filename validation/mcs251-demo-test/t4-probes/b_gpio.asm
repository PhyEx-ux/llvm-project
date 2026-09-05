; Probe B: STC32G GPIO P0-P7 under QEMU stc32g144k246.
;
; The model implements mode-dependent read sampling: with no external driver
; every pin input is pulled high (0xFF), so input/quasi/open-drain reads of a
; high latch return 1, and reads of a low latch return 0 unless the mode makes
; the pin a pure latch (push-pull).  A plain RAM would always return the last
; written byte; these checks distinguish real port behavior.
;
;   B1  reset values: P0=0xFF, P0M1=0xFF, P0M0=0x00
;   B2  input mode (reset default): write P0=0x00, read back = 0xFF
;   B3  push-pull (M1=0,M0=1): write 0x5A, read back 0x5A
;   B4  open-drain (M1=1,M0=1): write 0xA5, read back 0xA5 (inputs pull up)
;   B5  quasi-bidirectional (M1=0,M0=0): write 0x3C, read back 0x3C
;   B6  push-pull write/read sweep on P1,P2,P3,P4,P5,P6,P7
;
        .module probe_b_gpio
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
P0      = 0x80
P0M1    = 0x93
P0M0    = 0x94
P1      = 0x90
P1M1    = 0x91
P1M0    = 0x92
P2      = 0xa0
P2M1    = 0x95
P2M0    = 0x96
P3      = 0xb0
P3M1    = 0xb1
P3M0    = 0xb2
P4      = 0xc0
P4M1    = 0xb3
P4M0    = 0xb4
P5      = 0xc8
P5M1    = 0xc9
P5M0    = 0xca
P6      = 0xe8
P6M1    = 0xcb
P6M0    = 0xcc
P7      = 0xf8
P7M1    = 0xe1
P7M0    = 0xe2

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

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

; expect_eq: compare acc with r3; on mismatch print acc and FAIL.
; clobbers a
eqr3:
        xrl     a,r3
        jz      eq_ok
        xrl     a,r3                    ; recover got value
        ecall   pa                      ; evidence: got byte
        ejmp    fail
eq_ok:  eret

main:
        mov     a,#'b'
        ecall   putc

        ; B1: reset values (pa clobbers acc; reload before comparing)
        mov     a,P0
        ecall   pa
        mov     a,P0
        xrl     a,#0xff
        jz      b1a
        ejmp    fail
b1a:    mov     a,P0M1
        ecall   pa
        mov     a,P0M1
        xrl     a,#0xff
        jz      b1b
        ejmp    fail
b1b:    mov     a,P0M0
        ecall   pa
        mov     a,P0M0
        jz      b1ok
        ejmp    fail
b1ok:   mov     a,#'1'
        ecall   putc

        ; B2: input mode: latch low does not drag the pin down
        mov     P0,#0x00
        mov     a,P0
        ecall   pa                      ; expect 0xFF (pin input pulled high)
        mov     a,P0
        xrl     a,#0xff
        jz      b2ok
        ejmp    fail
b2ok:   mov     a,#'2'
        ecall   putc

        ; B3: push-pull reads back the latch
        mov     P0M1,#0x00
        mov     P0M0,#0xff
        mov     P0,#0x5a
        mov     a,P0
        ecall   pa
        mov     a,P0
        xrl     a,#0x5a
        jz      b3ok
        ejmp    fail
b3ok:   mov     a,#'3'
        ecall   putc

        ; B4: open-drain with pull-up inputs reads back the latch
        mov     P0M1,#0xff
        mov     P0M0,#0xff
        mov     P0,#0xa5
        mov     a,P0
        ecall   pa
        mov     a,P0
        xrl     a,#0xa5
        jz      b4ok
        ejmp    fail
b4ok:   mov     a,#'4'
        ecall   putc

        ; B5: quasi-bidirectional reads back the latch (inputs high)
        mov     P0M1,#0x00
        mov     P0M0,#0x00
        mov     P0,#0x3c
        mov     a,P0
        ecall   pa
        mov     a,P0
        xrl     a,#0x3c
        jz      b5ok
        ejmp    fail
b5ok:   mov     a,#'5'
        ecall   putc

        ; B6: push-pull sweep P1..P7, pattern 0xC3
        mov     r3,#0xc3
        mov     P1M1,#0x00
        mov     P1M0,#0xff
        mov     P1,r3
        mov     a,P1
        ecall   eqr3
        mov     P2M1,#0x00
        mov     P2M0,#0xff
        mov     P2,r3
        mov     a,P2
        ecall   eqr3
        mov     P3M1,#0x00
        mov     P3M0,#0xff
        mov     P3,r3
        mov     a,P3
        ecall   pa                      ; evidence for P3
        mov     a,P3
        ecall   eqr3
        mov     P4M1,#0x00
        mov     P4M0,#0xff
        mov     P4,r3
        mov     a,P4
        ecall   eqr3
        mov     P5M1,#0x00
        mov     P5M0,#0xff
        mov     P5,r3
        mov     a,P5
        ecall   eqr3
        mov     P6M1,#0x00
        mov     P6M0,#0xff
        mov     P6,r3
        mov     a,P6
        ecall   eqr3
        mov     P7M1,#0x00
        mov     P7M0,#0xff
        mov     P7,r3
        mov     a,P7
        ecall   eqr3
        mov     a,#'6'
        ecall   putc

        ejmp    pass

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
