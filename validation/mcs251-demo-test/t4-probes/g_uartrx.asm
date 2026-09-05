; Probe G: UART1 receive path under QEMU stc32g144k246.
;
; REN=1, poll RI, read SBUF, expect the byte piped into QEMU's stdio ('Z').
; Exercises stc32g_uart_can_receive/receive: rx_buffer -> SBUF read, RI set.
;
        .module probe_g_uartrx
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
SCON    = 0x98

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

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
        mov     a,#'g'
        ecall   putc
        mov     SCON,#0x10              ; REN=1

        ; poll RI (SCON.0) with a generous timeout
        mov     r5,#0x00                ; 256*65536 inner iterations
w1:     mov     r6,#0
w2:     mov     r7,#0
w3:     mov     a,SCON
        anl     a,#0x01                 ; RI
        jnz     got
        djnz    r7,w3
        djnz    r6,w2
        djnz    r5,w1
        ejmp    fail                    ; nothing received
got:
        mov     a,SBUF                  ; rx byte
        ecall   pa                      ; evidence: received byte (want 5A)
        mov     a,SBUF
        xrl     a,#'Z'
        jz      gok
        ejmp    fail
gok:
        mov     SCON,#0x00              ; clear RI/REN
        mov     a,#'1'
        ecall   putc
        ejmp    pass

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
