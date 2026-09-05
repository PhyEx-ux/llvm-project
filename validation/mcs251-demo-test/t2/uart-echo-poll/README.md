# T2 UART echo poll

The module enables `SCON.REN`, polls `RI` with a bounded timeout, reads
`SBUF`, clears only `RI`, writes the byte back to `SBUF`, then clears only `TI`
and polls for the cleared state. The build script injects `Z` through QEMU
stdin and expects the echoed `Z` in `expected.serial`.
