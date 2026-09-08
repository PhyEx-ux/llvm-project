# STC32G144K246 board profile used by the QEMU machine.
# Manual/QEMU research: Flash 0xFC2800..0xFFFFFF, 16 KiB EDATA and 128 KiB
# XDATA.  The demo deliberately keeps the same conservative FE/FF layout as
# the real-hardware profile so one source and one set of link rules serve both.
FLASH_BASE := 0xfc2800
# STC32G144K246: Flash 0xFC2800..0xFFFFFF = 0x3D800 bytes (246 KiB usable).
FLASH_SIZE := 0x3d800
EDATA_END := 0x3fff
CSEG_BASE := 0xff0200
XINIT_BASE := 0xff8000
EEPROM_MAX := 0x0700
HRIC := 24000000
BAUD := 115200
UART_RELOAD := 0xffcc
LED_PORT := P4
LED_BIT := 0x20
QEMU_MACHINE := stc32g144k246
QEMU_NOTE := Native QEMU machine; demo layout remains in the G12/G144 FE/FF Flash intersection
