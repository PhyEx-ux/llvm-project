; Probe C: UART2/3/4 SFRs under QEMU stc32g144k246.
;
; Source audit says only UART1 (SCON/SBUF) is device-backed; the CPU SFR
; window returns 0 and drops writes for every unlisted address.  These checks
; pin that down at runtime and contrast with the working UART1.
;
;   C1  S2CON(0x9A)/S2BUF(0x9B): write distinctive values, read back = 0x00
;   C2  S3CON(0xAC)/S3BUF(0xAD): same
;   C3  S4CON(0xFD)/S4BUF(0xFE): same
;   C4  contrast: SCON(0x98) latches 0x50
;   C5  contrast: SBUF write emits on the serial transcript ('U' visible)
;
        .module probe_c_uart234
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
SCON    = 0x98
S2CON   = 0x9a
S2BUF   = 0x9b
S3CON   = 0xac
S3BUF   = 0xad
S4CON   = 0xfd
S4BUF   = 0xfe

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

main:
        mov     a,#'c'
        ecall   putc

        ; C1: UART2 absent
        mov     S2CON,#0x50
        mov     a,S2CON
        ecall   pa
        mov     a,S2CON
        jz      c1a
        ejmp    fail
c1a:    mov     S2BUF,#0x55
        mov     a,S2BUF
        ecall   pa
        mov     a,S2BUF
        jz      c1ok
        ejmp    fail
c1ok:   mov     a,#'1'
        ecall   putc

        ; C2: UART3 absent
        mov     S3CON,#0x50
        mov     a,S3CON
        ecall   pa
        mov     a,S3CON
        jz      c2a
        ejmp    fail
c2a:    mov     S3BUF,#0x66
        mov     a,S3BUF
        ecall   pa
        mov     a,S3BUF
        jz      c2ok
        ejmp    fail
c2ok:   mov     a,#'2'
        ecall   putc

        ; C3: UART4 absent
        mov     S4CON,#0x50
        mov     a,S4CON
        ecall   pa
        mov     a,S4CON
        jz      c3a
        ejmp    fail
c3a:    mov     S4BUF,#0x77
        mov     a,S4BUF
        ecall   pa
        mov     a,S4BUF
        jz      c3ok
        ejmp    fail
c3ok:   mov     a,#'3'
        ecall   putc

        ; C4: UART1 SCON latches (mask TI/RI: our own putc sets TI)
        mov     SCON,#0x50
        mov     a,SCON
        ecall   pa
        mov     a,SCON
        anl     a,#0xfc
        xrl     a,#0x50
        jz      c4ok
        ejmp    fail
c4ok:   mov     a,#'4'
        ecall   putc
        mov     SCON,#0x00              ; leave TI/RI clean

        ; C5: UART1 SBUF emits on serial: transcript must show this 'U'
        mov     a,#'U'
        mov     SBUF,a
        mov     a,#'5'
        ecall   putc

        ejmp    pass

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
