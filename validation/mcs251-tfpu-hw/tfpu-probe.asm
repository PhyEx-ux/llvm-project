; ---------------------------------------------------------------------------
; TFPU status / completion probe for STC32G12K128.
;
; WHY THIS EXISTS
; The manual documents command 0x33 as "read status register, result saved to
; R7" but never defines the status bit layout, and the official
; AI8051U_32_TFPU.LIB disassembles to "MOV DMAIR,#cmd ; RET" (zero wait).
; This module collects the raw evidence on real silicon:
;
;   * raw R7 / SFR 0xEF / SFR 0xFF before any TFPU command
;   * R7 after 0x33 at four moments: clean, after 0x31, after 0x32, after a
;     multiply (and again once the product has been read out)
;   * SFR 0xEF and 0xFF at the same moments
;   * the first NOP count n at which R4..R7 hold the correct IEEE-754 result
;     for mul (0x1E), add (0x1C) and sin (0x2D)
;
; IMPORTANT CORRECTION TO THE BRIEF
; The bytes following "MOV DMAIR,#0x33" in the official library are
;     A5 EF  ->  MOV A,R7    (Source-mode MOV A,Rn = [A5][E8+rn]; rn=7 -> EF)
;     A5 FF  ->  MOV R7,A    (Source-mode MOV Rn,A = [A5][F8+rn]; rn=7 -> FF)
; They are NOT reads of SFR 0xEF / 0xFF.  On this part 0xEF is AUXINTIF and
; 0xFF is RSTCFG; the official library touches neither.  The probe still samples
; both, purely as a control that they do not move.
;
; ORDERING CONTRACT (user requirement)
; Every TFPU action -- the 0x3E clock select and 0x31/0x32 included -- happens
; strictly after the first UART output.  main.c owns UART and prints a heartbeat
; before and after each stage; this module only issues a TFPU command when
; called from C.
;
; REGISTER BANKS
; Bank 0 is the reset default and is what the C compiler uses, so R0..R7 map to
; DATA 0x00..0x07 (BR = R0..R3 with MSB in R0, AR = R4..R7 with MSB in R4).  No
; bank switch is performed: the compiler is not running inside the timed window
; and every entry point saves/restores R0..R15, PSW, PSW1 and DPX.
;
; LATENCY MEASUREMENT
; Each command owns one shared run of TPU_SLED NOPs (see gen-sled.py).  The
; dispatcher jumps into that run so that exactly n NOPs execute before the
; snapshot: PC = sled + (TPU_SLED - n).  Since A is only 8 bits wide, two DPTR
; bases are used (lo: n <= 255, hi: n > 255); main.c pre-computes A and the
; selector.  Only "JMP @A+DPTR" sits between the DMAIR trigger and the sled, so
; the residual fixed overhead is a single instruction (documented, constant,
; and small relative to the 26..270-clock commands under test).
; ---------------------------------------------------------------------------
        .module mcs251_tfpu_probe
        .source
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
        .globl _main, _start
        .globl _tpu_clk_sys, _tpu_init, _tpu_clr_exc
        .globl _tpu_raw, _tpu_read_state, _tpu_mul_once
        .globl _tpu_mul_at_n, _tpu_add_at_n, _tpu_sin_at_n

; SFRs
IE      = 0xa8
TCON    = 0x88
PSW     = 0xd0
PSW1    = 0xd1
DMAIR   = 0xed

; EDATA scratch / observation slots shared with main.c.  Fixed addresses only,
; so there are no C globals and therefore no XINIT / CRT data copy.
; 0x00..0x1F is the register bank and 0x20..0x2F is the bit-addressable area,
; hence the slots start at 0x0030.
S_R7RAW = 0x0030        ; R7 read directly, no 0x33 issued
S_R7    = 0x0031        ; R7 after 0x33
S_EF    = 0x0032        ; SFR 0xEF (AUXINTIF)
S_FF    = 0x0033        ; SFR 0xFF (RSTCFG)
S_A     = 0x0034        ; dispatch byte for JMP @A+DPTR
S_SEL   = 0x0035        ; 0 = lo sled base, 1 = hi sled base
S_L4    = 0x0036        ; snapshot of R4..R7 (MSB first)
S_L5    = 0x0037
S_L6    = 0x0038
S_L7    = 0x0039
S_MARK  = 0x003a        ; written last by each entry: completion witness

; Known operands and expected IEEE-754 single results (big-endian per window):
;   3.9f = 0x4079999A   5.1f = 0x40A33333   1.0f = 0x3F800000
;   mul  = 0x419F1EB8   add  = 0x41100000   sin(1.0f) = 0x3F576AA4

        .area HOME (CODE)
        ljmp _start

        .area VECS (CODE)
        .ds 8

        .area CSEG (CODE)

; Sled geometry and bodies.  Included here, before the dispatchers, because
; sdas251 resolves #(...) expressions where they appear: both the TPU_SLED*
; constants and the sled labels must already be known at the dispatch sites.
        .include "sled.inc"

.macro SAVEREGS
        push PSW
        push PSW1
        push dr0
        push dr4
        push dr8
        push dr12
        push dpx
.endm
.macro RESTREG
        pop dpx
        pop dr12
        pop dr8
        pop dr4
        pop dr0
        pop PSW1
        pop PSW
.endm

_start:
        mov IE,#0
        mov TCON,#0
        mov 0xe9,#0             ; WTST  = 0 (fastest XRAM/SFR access)
        mov 0xea,#0             ; CKCON = 0
        mov spx,#0x0800         ; C stack grows up from 0x0800 into 0x0FFF
        ecall _main
_stop:
        sjmp _stop

; --- 0x3E: select the system clock as the TFPU clock source ---------------
_tpu_clk_sys:
        mov DMAIR,#0x3e
        eret

; --- 0x31: initialise the co-processor (raises an exception state) --------
_tpu_init:
        mov DMAIR,#0x31
        eret

; --- 0x32: clear all exception states -------------------------------------
_tpu_clr_exc:
        mov DMAIR,#0x32
        eret

; --- raw sample: read R7 / 0xEF / 0xFF WITHOUT issuing any TFPU command ---
_tpu_raw:
        SAVEREGS
        mov a,r7
        mov S_R7RAW,a
        mov r11,0xef            ; MOV A,0xEF : AUXINTIF
        mov S_EF,r11
        mov r11,0xff            ; MOV A,0xFF : RSTCFG
        mov S_FF,r11
        mov S_MARK,#0x01
        RESTREG
        eret

; --- 0x33: read the TFPU status register (documented result in R7) --------
_tpu_read_state:
        SAVEREGS
        mov DMAIR,#0x33
        .rept 8                 ; 0x33 is documented as 4 clocks
        nop
        .endm
        mov S_R7,r7
        mov r11,0xef
        mov S_EF,r11
        mov r11,0xff
        mov S_FF,r11
        mov S_MARK,#0x02
        RESTREG
        eret

; --- one multiply with a generous wait, then read the status --------------
; Also acts as the TFPU-presence discriminator: if the part does not implement
; TFPU, R4..R7 stay exactly as loaded (0x4079999A) instead of becoming
; 0x419F1EB8.  The second 0x33 is issued *after* the product was read out, so
; it shows the status at the "result consumed" moment.
_tpu_mul_once:
        SAVEREGS
        mov wr0,#0x40A3         ; BR = 5.1f
        mov wr2,#0x3333
        mov wr4,#0x4079         ; AR = 3.9f
        mov wr6,#0x999A
        mov DMAIR,#0x1e
        .rept 64
        nop
        .endm
        mov S_L4,r4
        mov S_L5,r5
        mov S_L6,r6
        mov S_L7,r7
        mov DMAIR,#0x33
        .rept 8
        nop
        .endm
        mov S_R7,r7
        mov r11,0xef
        mov S_EF,r11
        mov r11,0xff
        mov S_FF,r11
        mov S_MARK,#0x03
        RESTREG
        eret

; --- latency scans --------------------------------------------------------
; Operand windows: BR = R0..R3 (5.1f), AR = R4..R7 (3.9f or 1.0f).
; The dispatch byte/selector are computed in C *before* the call; the trigger
; is issued immediately before JMP @A+DPTR so nothing but the jump separates
; the command from the sled.
_tpu_mul_at_n:
        SAVEREGS
        mov wr0,#0x40A3
        mov wr2,#0x3333
        mov wr4,#0x4079
        mov wr6,#0x999A
        sjmp _tpu_dispatch_mul

_tpu_add_at_n:
        SAVEREGS
        mov wr0,#0x40A3
        mov wr2,#0x3333
        mov wr4,#0x4079
        mov wr6,#0x999A
        sjmp _tpu_dispatch_add

_tpu_sin_at_n:
        SAVEREGS
        mov wr4,#0x3F80         ; AR = 1.0f radians
        mov wr6,#0x0000
        sjmp _tpu_dispatch_sin

; Dispatch: S_SEL picks the DPTR base (lo covers n <= 255, hi covers the rest),
; S_A carries the entry offset.  The DMAIR trigger is issued immediately before
; JMP @A+DPTR so only that single jump separates the command from the sled.
; Written out per command rather than macro-generated: sdas251 concatenates
; macro arguments with an apostrophe, which does not work inside #(...).
_tpu_dispatch_mul:
        mov a,S_SEL
        jz _tpu_mul_lo
        mov dptr,#(_tpu_mul_sled)
        sjmp _tpu_mul_go
_tpu_mul_lo:
        mov dptr,#(_tpu_mul_sled+TPU_SLED_LO)
_tpu_mul_go:
        mov a,S_A
        mov DMAIR,#0x1e
        jmp @a+dptr
_tpu_mul_done:
        mov S_MARK,#0x11
        RESTREG
        eret

_tpu_dispatch_add:
        mov a,S_SEL
        jz _tpu_add_lo
        mov dptr,#(_tpu_add_sled)
        sjmp _tpu_add_go
_tpu_add_lo:
        mov dptr,#(_tpu_add_sled+TPU_SLED_LO)
_tpu_add_go:
        mov a,S_A
        mov DMAIR,#0x1c
        jmp @a+dptr
_tpu_add_done:
        mov S_MARK,#0x12
        RESTREG
        eret

_tpu_dispatch_sin:
        mov a,S_SEL
        jz _tpu_sin_lo
        mov dptr,#(_tpu_sin_sled)
        sjmp _tpu_sin_go
_tpu_sin_lo:
        mov dptr,#(_tpu_sin_sled+TPU_SLED_LO)
_tpu_sin_go:
        mov a,S_A
        mov DMAIR,#0x2d
        jmp @a+dptr
_tpu_sin_done:
        mov S_MARK,#0x13
        RESTREG
        eret


