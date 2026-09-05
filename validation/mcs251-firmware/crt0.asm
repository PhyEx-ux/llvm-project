; MCS-251 firmware startup module maintained in-tree.
;
; This is intentionally a small, standalone replacement for the generated
; SDCC startup fragment used by the validation images.  HOME and _main live in
; the C harness; keeping them separate lets an LLVM module be linked without
; importing a transient SDCC build-tree crt0.
;
; The .optsdcc line is part of the ASxxxx/SDLD ABI contract.  Keep it byte for
; byte in sync with the compiler and with the -A line in link-template.lk.

        .module mcs251_firmware_crt0
        .source
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

        .globl __sdcc_gsinit_startup
        .globl __sdcc_program_startup
        .globl __mcs51_genRAMCLEAR
        .globl __mcs51_genXINIT
        .globl __mcs51_genXRAMCLEAR

        ; GSINIT0 is placed at the fixed startup address by the linker.  Set
        ; the extended stack pointer before entering SDCC's program startup so
        ; ECALL/ERET frames and C code have a known stack location.
        .area GSINIT0 (CODE)
__sdcc_gsinit_startup::
        mov     spx,#0x2fff
        ejmp    __sdcc_program_startup

        ; SDCC's startup sequence references these runtime hooks even when an
        ; image has no initialized data.  Empty ERET stubs keep the symbols
        ; resolvable while preserving the caller's three-byte return frame.
        .area CSEG (CODE)
__mcs51_genRAMCLEAR::
        eret
__mcs51_genXINIT::
        eret
__mcs51_genXRAMCLEAR::
        eret
