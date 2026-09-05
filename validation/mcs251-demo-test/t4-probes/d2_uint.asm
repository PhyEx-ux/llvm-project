; Probe D2: UART1 transmit interrupt under QEMU stc32g144k246.
;
; Vector: UART1 -> 0xFF0023.  Writing SBUF sets TI, which raises the level
; IRQ.  The UART IRQ is NOT auto-clear: the ISR must clear TI in SCON or the
; interrupt re-fires forever after RETI.  ISR prints 'U', clears TI, counts.
;
;   D2a  TI interrupt fires: FLAG set, CNT == 1 exactly
;   D2b  SCON sampled inside the ISR after the TI clear is saved to cell
;        0x32; main prints it and requires TI == 0 (manual clear worked).
;
; NOTE: with ES enabled every SBUF write raises the IRQ, so main disables
; IE before printing anything after the first hit.
;
        .module probe_d2_uint
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
SCON    = 0x98
IE      = 0xa8
FLAG    = 0x30
CNT     = 0x31

        .area HOME (CODE)
start:
        mov     spx,#0x2fff
        ejmp    main

        .area VEC (CODE)
        ejmp    isr_u1                  ; linked at 0xff0023

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
        mov     a,#'2'
        ecall   putc

        mov     FLAG,#0
        mov     CNT,#0
        mov     SCON,#0x00              ; RI/TI clean
        mov     IE,#0x90                ; EA|ES
        mov     SBUF,#'x'               ; transmit -> TI -> IRQ

        ; poll FLAG with timeout
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
        mov     IE,#0x00                ; stop the IRQ before any printing
        mov     a,CNT
        ecall   pa                      ; evidence: ISR entry count
        mov     a,CNT
        xrl     a,#0x01
        jnz     failj                   ; must be exactly 1 (no storm)
        mov     a,0x32
        ecall   pa                      ; evidence: SCON sampled in ISR
        mov     a,0x32
        anl     a,#0x02                 ; TI bit as seen after the clear
        jnz     failj
        mov     a,#'K'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

; ---- ISR: UART1 vector target ---------------------------------------
; Order matters: the 'U' print sets TI again, then we clear it.
isr_u1:
        mov     SBUF,#'U'
        mov     a,SCON
        anl     a,#0xfd                 ; TI=0
        mov     SCON,a
        mov     a,SCON                  ; sample for evidence (TI reads 0)
        mov     0x32,a
        inc     CNT
        mov     FLAG,#1
        reti

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
