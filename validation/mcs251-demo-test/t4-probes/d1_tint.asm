; Probe D1: Timer0 overflow interrupt under QEMU stc32g144k246.
;
; Vector: TF0 -> 0xFF000B (classic 8051 layout relocated to flash top).
; ISR prints 'I', increments CNT, sets FLAG, RETI.
; Main arms EA|ET0, runs T0 mode1 with a short countdown, polls FLAG with a
; timeout, then checks TF0 was auto-cleared on interrupt entry.
;
; Area layout (no overlaps):
;   HOME  0xff0000  reset stub only (7 bytes)
;   VEC   0xff000b  TF0 vector: ejmp isr_t0
;   PROBE 0xff0100  main + utilities + ISR
;
        .module probe_d1_tint
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
        ejmp    isr_t0                  ; linked at 0xff000b

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
        mov     a,#'1'
        ecall   putc

        mov     FLAG,#0
        mov     CNT,#0
        mov     TMOD,#0x01              ; T0 mode1 16-bit
        mov     TH0,#0xff
        mov     TL0,#0xf0               ; 16 ticks to overflow
        mov     IE,#0x82                ; EA|ET0
        orl     TCON,#0x10              ; TR0=1

        ; poll FLAG with timeout (~64*65536 inner iterations)
        mov     r5,#0x40
w1:     mov     r6,#0
w2:     mov     r7,#0
w3:     mov     a,FLAG
        jnz     got
        djnz    r7,w3
        djnz    r6,w2
        djnz    r5,w1
        ejmp    fail                    ; interrupt never fired
got:
        anl     TCON,#0xef              ; TR0=0
        mov     a,CNT
        ecall   pa                      ; evidence: ISR entry count
        mov     a,CNT
        jz      failj                   ; CNT must be >= 1
        ; D1b: TF0 auto-cleared on entry?
        mov     a,TCON
        ecall   pa                      ; evidence: TCON after ISR
        mov     a,TCON
        anl     a,#0x20
        jnz     failj                   ; TF0 still set -> no auto-clear
        mov     a,#'K'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

; ---- ISR: TF0 vector target -----------------------------------------
; Keeps acc untouched: only direct-address writes.
isr_t0:
        mov     SBUF,#'I'
        inc     CNT
        mov     FLAG,#1
        reti

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
