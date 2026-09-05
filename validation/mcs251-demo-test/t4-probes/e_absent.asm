; Probe E: peripherals with no device model under QEMU stc32g144k246.
;
; The CPU SFR window returns 0 and drops writes for every address without a
; device or CPU special case.  These checks pin the runtime behavior of the
; ADC, watchdog and a sample of other official-demo SFRs, plus one XFR cell
; in the 0x7EFE00 window that has no mapped MemoryRegion.
;
;   E1  ADC_CONTR(0xBC)=write 0x80 -> read 0x00; ADC_RES(0xBD)=0x00;
;       ADC_RESL(0xBE)=0x00
;   E2  WDT_CONTR(0xC1)=write 0x34 -> read 0x00
;   E3  I2C/PWM/CAN sample SFRs read 0x00 (I2CCFG 0xFE80 is XFR; here SFR
;       space samples: PWM0CR? none in SFR; use PCA CCON 0xD8? DPUOP lives
;       there, skip; sample I2C SFRs are XFR-only on this part)
;   E4  XFR window unmapped cell 0x7EFE80 (ADCTIM area): evidence only,
;       printed, not asserted (QEMU unassigned-bus semantics)
;
        .module probe_e_absent
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
ADC_CONTR = 0xbc
ADC_RES = 0xbd
ADC_RESL = 0xbe
WDT_CONTR = 0xc1
P_SW2   = 0xba

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
        mov     a,#'e'
        ecall   putc

        ; E1: ADC absent
        mov     ADC_CONTR,#0x80
        mov     a,ADC_CONTR
        ecall   pa
        mov     a,ADC_CONTR
        jz      e1a
        ejmp    fail
e1a:    mov     a,ADC_RES
        ecall   pa
        mov     a,ADC_RES
        jz      e1b
        ejmp    fail
e1b:    mov     a,ADC_RESL
        ecall   pa
        mov     a,ADC_RESL
        jz      e1ok
        ejmp    fail
e1ok:   mov     a,#'1'
        ecall   putc

        ; E2: watchdog absent
        mov     WDT_CONTR,#0x34
        mov     a,WDT_CONTR
        ecall   pa
        mov     a,WDT_CONTR
        jz      e2ok
        ejmp    fail
e2ok:   mov     a,#'2'
        ecall   putc

        ; E4: unmapped XFR cell 0x7EFE80 - evidence only (no assert)
        mov     P_SW2,#0x80             ; EAXFR=1 opens 0x7e0000 window
        mov     dptr,#0xfe80
        mov     dpxl,#0x7e
        mov     a,#0x5a
        mov     @dpx,a
        mov     a,@dpx
        ecall   pa                      ; evidence: unmapped XFR read
        mov     a,#'4'
        ecall   putc

        ejmp    pass

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
