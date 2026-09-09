; Hardware frame capture. C calls with ECALL; the wrapper returns with ERET.
        .module mcs251_hwframe
        .source
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
        .globl _main, _start, _hwf_measure, _hwf_isr
        .globl _hwf_wait, _hwf_wait_end, _hwf_reti
SP = 0x81
SPH = 0x85
PSW = 0xd0
PSW1 = 0xd1
IE = 0xa8
TCON = 0x88
TMOD = 0x89
SBUF = 0x99
SCON = 0x98
        .area HOME (CODE)
        ljmp _start
        .area VECS (CODE)
        .ds 8
        ejmp _hwf_isr
        .area CSEG (CODE)
_start:
        mov IE,#0
        mov TCON,#0
        mov 0xe9,#0
        mov 0xea,#0
        mov spx,#0x0800
        ecall _main
_stop:
        sjmp _stop

; Save every register touched by the wrapper/ISR on the C stack. Scratch
; 0040..0052, capture 0400..041F and measurement 01F0..020F are reserved.
_hwf_measure:
        push PSW
        push PSW1
        push dr0
        push dr4
        push dr8
        push dpx
        mov 0x50,SP
        mov 0x51,SPH
        mov IE,#0
        mov TCON,#0
        mov wr8,#0x0040
        mov r6,#16
        clr a
_clear:
        mov @wr8,r11
        inc wr8
        djnz r6,_clear
        mov wr8,#0x01f0
        mov r6,#32
        mov a,#0xa5
_fill:
        mov @wr8,r11
        inc wr8
        djnz r6,_fill
        mov spx,#0x01fe
        mov TMOD,#1
        mov 0x8c,#0xf0
        mov 0x8a,#0
        mov 0x40,SP
        mov 0x41,SPH
        mov 0x43,0xbe
        mov r3,#0
        mov r4,#0
        mov 0x42,PSW1
        ; The test owns IE and Timer0; only ET0 is enabled.
        mov IE,#0x82
        orl TCON,#0x10
_hwf_wait:
        mov a,0x4c
        jnz _hwf_wait_end
        djnz r3,_hwf_wait
        djnz r4,_hwf_wait
_hwf_wait_end:
        mov 0x4a,PSW1
        mov 0x48,SP
        mov 0x49,SPH
        mov 0x4b,0xbe
        mov IE,#0
        mov TCON,#0
        mov SPH,0x51
        mov SP,0x50
        pop dpx
        pop dr8
        pop dr4
        pop dr0
        pop PSW1
        pop PSW
        eret

; No stack operations until RETI. Capture PSW1 before flag-changing moves.
; Copy the entire fixed window before UART or any C code can reuse memory.
_hwf_isr:
        mov 0x46,PSW1
        mov 0x44,SP
        mov 0x45,SPH
        mov 0x47,0xbe
        anl TCON,#0xef
        mov wr8,#0x01f0
        mov wr0,#0x0400
        mov r6,#32
_copy:
        mov r11,@wr8
        mov @wr0,r11
        inc wr8
        inc wr0
        djnz r6,_copy
        mov 0x4c,#1
        ; An unbuffered marker survives even if the subsequent RETI fails.
        ; RUN has drained TI before entry; the C reporter drains this byte.
        anl SCON,#0xfd
        mov SBUF,#'I'
_hwf_reti:
        reti
