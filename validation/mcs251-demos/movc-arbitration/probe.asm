; G12K128 MOVC arbitration. Built by build.py with REAL_HW=0 or 1.
; Execute MOVC in FE:0800, same DPTR offset points at distinct FE/FF sentinels.
; ISP EEPROM partition must be <=0x700 bytes (zero recommended). The old
; FE:0200 body overlapped a 1K EEPROM partition and could not execute.
; No compiler, MOVC-dependent strings, XINIT, interrupts or RAM execution.
; Manual p1589 example annotates DPTR0=#1000H as FF1000H (strong evidence).
; p1655's EA=(A)+(DPTR) wording does not explicitly define the bank. This
; experiment arbitrates G12 silicon: A7 supports fixed FF, 3C current FE.
; Both full-address control reads must match before interpreting MOVC.
        .source
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
        .globl __mcs251_stack_base
        .globl movc_site
        .globl sentinel_fe
        .globl sentinel_ff
AUXR = 0x8e
SCON = 0x98
SBUF = 0x99
P_SW1 = 0xa2
P3M1 = 0xb1
P3M0 = 0xb2
T2H = 0xd6
T2L = 0xd7
        .area HOME (CODE)
        ljmp boot
        .area VECS (CODE)
        .rept 8
        ejmp unhandled
        .ds 4
        .endm
        .area BOOT (CODE)
boot:
        mov 0xe9,#0 ; WTST, manual p553: 24MHz profile uses zero wait states.
        mov spx,#__mcs251_stack_base
        .if REAL_HW
        ; ISP HIRC=24MHz, UART1 115200/8N1, Timer2 reload FFCC.
        mov P_SW1,#0x00 ; V1 silicon-tested initialization, reset value not measured.
        anl P3M1,#0xfc
        anl P3M0,#0xfc
        anl AUXR,#0xef
        anl AUXR,#0xf7
        mov SCON,#0x50
        mov T2L,#0xcc
        mov T2H,#0xff
        orl AUXR,#0x01
        orl AUXR,#0x04
        orl AUXR,#0x10
        .endif
        ejmp test
        .area CSEG (CODE)
test:
        mov dptr,#0x8000
        mov a,#0
movc_site:
        movc a,@a+dptr
        mov r4,a
        ; Control reads use explicit full 24-bit addresses, not MOVC.
        mov dr0,#0x8000
        movh dr0,#0x00fe
        mov r5,@dr0
        movh dr0,#0x00ff
        mov r6,@dr0
        mov r0,#'M'
        ecall putc
        mov r0,#'O'
        ecall putc
        mov r0,#'V'
        ecall putc
        mov r0,#'C'
        ecall putc
        mov r0,#'='
        ecall putc
        mov r1,r4
        ecall hexbyte
        mov r0,#' '
        ecall putc
        mov r0,#'F'
        ecall putc
        mov r0,#'E'
        ecall putc
        mov r0,#'='
        ecall putc
        mov r1,r5
        ecall hexbyte
        mov r0,#' '
        ecall putc
        mov r0,#'F'
        ecall putc
        mov r0,#'F'
        ecall putc
        mov r0,#'='
        ecall putc
        mov r1,r6
        ecall hexbyte
        mov r0,#10
        ecall putc
halt:
        .if REAL_HW
        ; Re-emit the observation for a terminal opened after programming.
        ; Explicit assembly counters cannot be optimized away; time uncalibrated.
        mov wr8,#200
repeat_outer:
        mov wr12,#4000
repeat_inner:
        dec wr12
        jne repeat_inner
        dec wr8
        jne repeat_outer
        ejmp test
        .else
        sjmp halt
        .endif
hexbyte:
        mov r0,r1
        srl r0
        srl r0
        srl r0
        srl r0
        ecall hexnib
        mov r0,r1
        anl r0,#15
        ecall hexnib
        eret
hexnib:
        cmp r0,#10
        jc decimal
        add r0,#7
decimal:
        add r0,#48
putc:
        mov SBUF,r0
        .if REAL_HW
wait_ti:
        mov a,SCON
        anl a,#2
        jz wait_ti
        anl SCON,#0xfd
        .endif
        eret
unhandled:
        mov r0,#'!'
        ecall putc
        sjmp halt
        .area SENT_FE (CODE)
sentinel_fe:
        .db 0x3c
        .area SENT_FF (CODE)
sentinel_ff:
        .db 0xa7
