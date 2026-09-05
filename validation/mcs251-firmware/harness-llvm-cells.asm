; harness-llvm-cells.asm - fixed idata cells backing harness-llvm.ll's
; multi-arg workaround convention (see that file's header).
;
; The cells are absolute equates in RSEG (ABS,DATA), the same pattern as
; provider.asm's _p13_mem: no storage is emitted, the linker only resolves
; the symbols, and the cells live in idata 0x60-0x66 (clear of register
; banks 0x00-0x1F and the T4 probe scratch cells 0x30-0x34).  Nothing
; initializes them; test modules store `expected` before every check call.
        .module mcs251_harness_llvm_cells
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1

        .globl  _harness_expect8
        .globl  _harness_expect16
        .globl  _harness_expect32

        .area RSEG (ABS,DATA)
_harness_expect8  = 0x0060
_harness_expect16 = 0x0061
_harness_expect32 = 0x0063
