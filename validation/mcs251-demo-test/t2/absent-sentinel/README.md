# T2 absent-peripheral sentinel

The measured QEMU model returns zero and drops writes for absent peripherals.
This case writes distinctive values to UART2 `S2CON`/`S2BUF`, `ADC_CONTR`,
`ADC_RES`, `ADC_RESL`, and `WDT_CONTR`, then asserts a zero readback for each.
Addresses are taken from `STC32G144K246.h` and the behavior is taken from
`/home/liu/mcs251-qemu-periph/REPORT.md`, not inferred from the hardware header.
