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
