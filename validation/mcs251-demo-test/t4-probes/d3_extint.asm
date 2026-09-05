; Probe D3: external interrupt INT0 under QEMU stc32g144k246, driven purely
; from software through the GPIO->timer-gate path.
;
; The model wires GPIO P3.2 pin output to the timer block's gate input 0,
; which doubles as the INT0 line (TCON IE0/IT0 handled there).  Putting P3.2
; in push-pull, driving it high then low, creates a falling edge that sets
; IE0 and raises INT0 -> vector 0xFF0003.
;
;   D3a  INT0 fires on software-driven P3.2 falling edge (edge mode IT0=1)
;   D3b  IE0 auto-cleared on entry: TCON.1 == 0 after ISR
;
        .module probe_d3_extint
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

SBUF    = 0x99
TCON    = 0x88
IE      = 0xa8
P3      = 0xb0
P3M1    = 0xb1
P3M0    = 0xb2
FLAG    = 0x30
CNT     = 0x31

; Area layout: INT0 vector 0xff0003 sits only 3 bytes after the reset entry,
; so the reset stub is a bare 2-byte SJMP and the stack setup lives in PROBE.
;   HOME  0xff0000  sjmp boot (2 bytes)
;   VEC   0xff0003  ejmp isr_x0 (4 bytes)
;   PROBE 0xff0008  boot/main/utilities/ISR (TF0 vector unused in this probe)
        .area HOME (CODE)
start:
        ljmp    0x0008                  ; -> (pc & 0xff0000)|0x0008 = boot

        .area VEC (CODE)
        ejmp    isr_x0                  ; linked at 0xff0003

        .area PROBE (CODE)
boot:
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
        mov     a,#'d'
        ecall   putc
        mov     a,#'3'
        ecall   putc

        mov     FLAG,#0
        mov     CNT,#0
        ; P3.2 push-pull output: M1.2=0, M0.2=1
        anl     P3M1,#0xfb
        orl     P3M0,#0x04
        orl     P3,#0x04                ; pin high
        orl     TCON,#0x01              ; IT0=1 (edge triggered)
        mov     IE,#0x81                ; EA|EX0
        anl     P3,#0xfb                ; falling edge -> IE0 -> INT0

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
        mov     a,CNT
        ecall   pa                      ; evidence: ISR entry count
        mov     a,CNT
        jz      failj
        mov     a,TCON
        ecall   pa                      ; evidence: TCON after ISR
        mov     a,TCON
        anl     a,#0x02                 ; IE0
        jnz     failj                   ; IE0 must be auto-cleared
        mov     a,#'K'
        ecall   putc
        ejmp    pass
failj:  ejmp    fail

; ---- ISR: INT0 vector target -----------------------------------------
isr_x0:
        mov     SBUF,#'E'
        inc     CNT
        mov     FLAG,#1
        reti

; ---- sdld driver stubs ----
        .area XSEG    (XDATA)
        .area PSEG    (PAG,XDATA)
