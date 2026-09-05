; Probe F: T2/T3/T4 absence + TM0PS prescaler functional effect.
;
;   F1  T2/T3/T4 SFRs (T4T3M 0xDD, T4H 0xD2, T4L 0xD3, T3H 0xD4, T3L 0xD5,
;       T2H 0xD6, T2L 0xD7): write patterns, read back = 0x00 (absent)
;   F2  TM0PS functional: same fixed instruction delay, T0 mode1 from 0:
;         baseline  (TM0PS=0x00, divider 12):   TH0 must be nonzero
;         prescaled (TM0PS=0xFF, divider 3072): TH0 must be zero
;       Margins are wide: delay window is ~1ms..30ms virtual, baseline needs
;       >2048 ticks (1ms), prescaled needs <256 ticks (32.8ms).
;
        .module probe_f_t234_presc
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
TMOD    = 0x89
TL0     = 0x8a
TH0     = 0x8c
P_SW2   = 0xba
T4T3M   = 0xdd
T4H     = 0xd2
T4L     = 0xd3
T3H     = 0xd4
T3L     = 0xd5
T2H     = 0xd6
T2L     = 0xd7

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

; delay4: ~262144 djnz iterations.  Clobbers r5,r6,r7.
delay4:
        mov     r5,#0x04
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

main:
        mov     a,#'f'
        ecall   putc

        ; F1: T2/T3/T4 absent
        mov     T4T3M,#0x77
        mov     a,T4T3M
        jz      f1a
        ejmp    fail
f1a:    mov     T4H,#0x11
        mov     a,T4H
        jz      f1b
        ejmp    fail
f1b:    mov     T4L,#0x22
        mov     a,T4L
        jz      f1c
        ejmp    fail
f1c:    mov     T3H,#0x33
        mov     a,T3H
        jz      f1d
        ejmp    fail
f1d:    mov     T3L,#0x44
        mov     a,T3L
        jz      f1e
        ejmp    fail
f1e:    mov     T2H,#0x55
        mov     a,T2H
        jz      f1f
        ejmp    fail
f1f:    mov     T2L,#0x66
        mov     a,T2L
        jz      f1ok
        ejmp    fail
f1ok:   mov     a,#'1'
        ecall   putc

        ; F2 baseline: TM0PS=0 -> delta into r3:r4
        mov     P_SW2,#0x80             ; EAXFR=1
        mov     dptr,#0xfea0
        mov     dpxl,#0x7e
        clr     a
        mov     @dpx,a                  ; TM0PS=0
        mov     TMOD,#0x01
        mov     TH0,#0x00
        mov     TL0,#0x00
        orl     TCON,#0x10              ; TR0=1
        ecall   delay4
        anl     TCON,#0xef              ; TR0=0
        mov     a,TH0
        mov     r3,a
        mov     a,TL0
        mov     r4,a
        mov     a,r3
        ecall   pa                      ; evidence: baseline TH0
        mov     a,r4
        ecall   pa                      ; evidence: baseline TL0
        mov     a,r3
        orl     a,r4
        jnz     f2a
        ejmp    fail                    ; baseline counter did not move
f2a:
        ; F2 prescaled: TM0PS=0xFF (divider 12*256 = 3072) -> delta r1:r2
        mov     dptr,#0xfea0
        mov     dpxl,#0x7e
        mov     a,#0xff
        mov     @dpx,a
        mov     TH0,#0x00
        mov     TL0,#0x00
        orl     TCON,#0x10
        ecall   delay4
        anl     TCON,#0xef
        mov     a,TH0
        mov     r1,a
        mov     a,TL0
        mov     r2,a
        mov     a,r1
        ecall   pa                      ; evidence: prescaled TH0
        mov     a,r2
        ecall   pa                      ; evidence: prescaled TL0
        ; scaled = presc_delta << 4 into r1:r2 (expected ratio 256, need >=16)
        mov     a,r2
        swap    a
        anl     a,#0xf0
        mov     r0,a                    ; new_lo
        mov     a,r2
        swap    a
        anl     a,#0x0f
        mov     r2,a
        mov     a,r1
        swap    a
        anl     a,#0xf0
        orl     a,r2
        mov     r1,a                    ; new_hi
        mov     r2,r0
        ; require base(r3:r4) >= scaled(r1:r2)
        clr     c
        mov     a,r4
        subb    a,r2
        mov     a,r3
        subb    a,r1
        jnc     f2ok
        ejmp    fail                    ; prescaler effect < 16x
f2ok:   mov     a,#'2'
        ecall   putc

        ; restore TM0PS=0
        mov     dptr,#0xfea0
        mov     dpxl,#0x7e
        clr     a
        mov     @dpx,a

        ejmp    pass

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
