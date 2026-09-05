; External-function provider sample for the MCS-251 validation images.
;
; The provider is intentionally an ASxxxx module rather than C.  It models a
; separately supplied routine and data symbol: LLVM emits ECALL _p13_ext and
; an external reference to _p13_mem, while this module supplies both symbols.
; Replace these bodies with the device/service being tested; keep the ABI
; signature and ERET calling convention unchanged.

        .module mcs251_firmware_provider
        .source
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

        .globl _p13_init
        .globl _p13_ext
        .globl _p13_mem

        ; Initialize a byte consumed by the LLVM load-global checkpoint.  The
        ; absolute data assignment below avoids pulling a transient data image
        ; or C runtime into this provider sample.
        .area CSEG (CODE)
_p13_init:
        mov     a,#0xa7
        mov     dptr,#_p13_mem
        mov     dpxl,#(_p13_mem >> 16)
        mov     @dpx,a
        eret

        ; Add five to the ABI's low-byte argument and return it in DPL.  ECALL
        ; pushes the three-byte return frame; every provider routine therefore
        ; returns with ERET, not RET.
_p13_ext:
        mov     r0,dpl
        add     r0,#0x05
        mov     dpl,r0
        eret

        ; A stable absolute XDATA-visible byte for the load-global example.
        .area RSEG (ABS,DATA)
_p13_mem = 0x0020

; ---------------------------------------------------------------------------
; SDCC runtime helpers (appended 2026-09-05, Moka / mcs251-provider-moka).
;
; The mcs251 runtime library never shipped these objects
; (device/lib/mcs251/Makefile.in OBJ lists only crt*/gptr_cmp/atomic_flag),
; while the compiler (src/mcs251/gen.c, gen_lower.c.inc) emits ECALLs to
; them for generic-pointer access and 16-bit mul/div.  The strict chain
; (mcs251_ld.py --mcs251-abi, link file carries -A) therefore reported
; undefined globals:
;   __gptrget / __gptrput              rtc-step, stack-packed,
;                                      stack-pop-packed, operator-precedence
;   __mulint / __mulint_PARM_2,
;   __divuint / __divuint_PARM_2       temperature-lookup
; The __*_PARM_2 symbols are the helpers' static second-argument slots of
; the non-reentrant ABI; the kernel module references them, and SDCC's own
; library module defines them next to the function body -- so they arrive
; together here.
;
; Bodies below are the assembly SDCC 4.6.0 #16555 generates for exactly this
; ABI (same .optsdcc signature as this module), compiled with:
;   /home/liu/build-sdcc/bin/sdcc -mmcs251 --model-small \
;       -I/home/liu/sdcc-src/device/include -c <source>.c
; from the upstream SDCC sources (/home/liu/sdcc-src, mirrored at
; /mnt/c/Prj/LLVM/MCS251/sdcc-upstream):
;   device/lib/_gptrget.c   mcs251 branch of the small-model __asm block
;   device/lib/_gptrput.c   mcs251 branch of the small-model __asm block
;   device/lib/_mulint.c    generic C path (mcs51 asm branches don't apply)
;   device/lib/_divuint.c   generic C path (shift-subtract loop)
; The harvested compiler output is archived in
; /home/liu/mcs251-provider-moka/harvest/*.asm.
;
; Conventions (read off the compiler's own call sites):
;   A generic pointer arrives in dpx (dpxl:dph:dpl = flat 24-bit address;
;   MCS-251 has a unified address space, so the mcs51 type-byte dispatch is
;   not emitted).  __gptrget returns the byte in A; __gptrput stores the
;   byte in A.  __mulint/__divuint take arg1 in dptr (dph=hi, dpl=lo), arg2
;   in the static __<fn>_PARM_2 slot (big-endian: hi at +0, lo at +1), and
;   return the 16-bit result in dptr (dph=hi, dpl=lo).
; ---------------------------------------------------------------------------

        .globl __gptrget
        .globl __gptrput
        .globl __gptrput_PARM_2
        .globl __mulint
        .globl __mulint_PARM_2
        .globl __divuint
        .globl __divuint_PARM_2
        .globl _B_5
        .globl _B_6
        .globl _B_7

        ; Bit addresses inside the B SFR, exported by SDCC's generated
        ; _gptrget/_gptrput modules; the mcs251 code paths never use them.
_B_7    =       0x00f7
_B_6    =       0x00f6
_B_5    =       0x00f5

ar7     =       0x07
ar6     =       0x06
ar5     =       0x05
ar4     =       0x04
ar3     =       0x03
ar2     =       0x02
ar1     =       0x01
ar0     =       0x00

        ; Register-bank-0 reservation, identical to every SDCC-emitted module.
        .area REG_BANK_0 (REL,OVR,DATA)
        .ds 8

        ; Static parameter/spill slots (non-reentrant ABI: overlay area).
        .area OSEG (OVR,DATA)
__gptrput_PARM_2:
        .ds 1
__mulint_PARM_2:
        .ds 2
__mulint_a_10000_162:
        .ds 2
__mulint_t_10000_163:
        .ds 2
__divuint_PARM_2:
        .ds 2

        .area CSEG (CODE)

        ; device/lib/_gptrget.c (mcs251 branch)
__gptrget:
        mov     a,@dpx
        eret

        ; device/lib/_gptrput.c (mcs251 branch)
__gptrput:
        mov     @dpx,a
        eret

        ; device/lib/_mulint.c (SDCC codegen of the generic C path)
__mulint:
        mov     (__mulint_a_10000_162 + 1),dpl
        mov     __mulint_a_10000_162,dph
        mov     r7,(__mulint_a_10000_162 + 1)
        mov     r6,(__mulint_PARM_2 + 1)
        mov     b,r7
        mov     a,r6
        mul     ab
        mov     (__mulint_t_10000_163 + 0),b
        mov     (__mulint_t_10000_163 + 1),a
        mov     r7,__mulint_t_10000_163
        mov     r6,(__mulint_a_10000_162 + 1)
        mov     r5,__mulint_PARM_2
        mov     b,r6
        mov     a,r5
        mul     ab
        mov     r6,a
        mov     r5,__mulint_a_10000_162
        mov     r4,(__mulint_PARM_2 + 1)
        mov     b,r5
        mov     a,r4
        mul     ab
        add     a,r6
        add     a,r7
        mov     __mulint_t_10000_163,a
        mov     dph,(__mulint_t_10000_163 + 0)
        mov     dpl,(__mulint_t_10000_163 + 1)
        eret

        ; device/lib/_divuint.c (SDCC codegen of the generic C path)
__divuint:
        mov     r7, dpl
        mov     r6, dph
        mov     r5,#0x00
        mov     r4,#0x00
        mov     r3,#0x10
00105$:
        mov     a,r6
        rl      a
        anl     a,#0x01
        mov     r2,a
        mov     a,r7
        add     a,r7
        mov     r7,a
        mov     a,r6
        rlc     a
        mov     r6,a
        mov     a,r5
        add     a,r5
        mov     r5,a
        mov     a,r4
        rlc     a
        mov     r4,a
        mov     a,r2
        jnz     00139$
        ejmp    00102$
00139$:
        orl     ar5,#0x01
00102$:
        clr     c
        mov     a,r5
        subb    a,(__divuint_PARM_2 + 1)
        mov     a,r4
        subb    a,__divuint_PARM_2
        jnc     00140$
        ejmp    00106$
00140$:
        mov     a,r5
        clr     c
        subb    a,(__divuint_PARM_2 + 1)
        mov     r5,a
        mov     a,r4
        subb    a,__divuint_PARM_2
        mov     r4,a
        orl     ar7,#0x01
00106$:
        dec     r3
        mov     a,r3
        jz      00141$
        ejmp    00105$
00141$:
        mov     dpl, r7
        mov     dph, r6
        eret
