# T2 SFR/UART TX

This case writes four bytes directly to UART1 `SBUF` (0x99), then writes and
reads `SCON` (0x98). QEMU's UART1 reset/readback value is 0x50; the module also
checks a subsequent 0x55 latch. The serial oracle is `expected.serial`.

`build.sh` emits `sfr-uart-tx.asm` before assembling it and fails unless the
constant SFR accesses use direct-address assembly operands.
