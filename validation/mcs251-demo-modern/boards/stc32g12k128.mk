# STC32G12K128 board profile.
# Memory limits and the 24 MHz UART setting are backed by real-hardware
# bring-up.  QEMU currently substitutes its G144 machine; the linked image
# stays in the FE/FF Flash intersection and retains the 4 KiB EDATA gate.
FLASH_BASE := 0xfe0000
EDATA_END := 0x0fff
CSEG_BASE := 0xff0200
XINIT_BASE := 0xff8000
EEPROM_MAX := 0x0700
HRIC := 24000000
BAUD := 115200
UART_RELOAD := 0xffcc
LED_PORT := P4
LED_BIT := 0x20
QEMU_MACHINE := stc32g144k246
QEMU_NOTE := G12 real-hardware profile; QEMU substitutes the G144 machine, using only the verified FE/FF Flash intersection
