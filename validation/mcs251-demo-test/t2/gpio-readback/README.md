# T2 GPIO readback

This case follows the measured `b_gpio` probe. It verifies that P0 in input
mode (`P0M1=0xff`, `P0M0=0x00`) reads `0xff` after writing a zero latch, then
verifies push-pull latch readback on P0 (`0x5a`) and P1 (`0xc3`).

The LLVM module uses volatile fixed-address pointers for P0/P1 and their mode
registers. `build.sh` emits and checks the LLVM assembly for direct operands
before producing the object image.
