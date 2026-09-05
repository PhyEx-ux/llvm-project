# T2 display table and bit scan

This is a demo-03-shaped mixed case. The LLVM module owns a ten-entry
seven-segment `t_display` table, looks up digit 3 (`0x4f`), and scans its
seven bits mathematically. The computed number of lit segments (4) is written
to UART1 `SBUF` and is also checked by the harness. The control byte `0x05`
in `expected.serial` is intentional; the harness emits a hex diagnostic if the
algorithm result ever differs.
