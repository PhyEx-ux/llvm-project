; Minimal startup hooks for the standalone SDCC/LLVM smoke image.
; The SDCC-generated harness owns HOME and its reset trampoline.  This module
; supplies only the GSINIT entry and runtime hooks normally provided by the
; device library.

        .module mcs251_smoke_crt0
        .source
@OPTSDCC@

        .globl __sdcc_gsinit_startup
        .globl __sdcc_program_startup
        .globl __mcs51_genRAMCLEAR
        .globl __mcs51_genXINIT
        .globl __mcs51_genXRAMCLEAR

        .area GSINIT0 (CODE)
__sdcc_gsinit_startup::
        mov     spx,#0x2fff
        ejmp    __sdcc_program_startup

        .area CSEG (CODE)
__mcs51_genRAMCLEAR::
        eret
__mcs51_genXINIT::
        eret
__mcs51_genXRAMCLEAR::
        eret
