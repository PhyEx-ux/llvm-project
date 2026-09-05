typedef unsigned char u8; typedef unsigned int u16; typedef unsigned long u32;
u8 u8g; u16 u16g;
extern u8 read_u8(void);
extern u8 read_u16_hi(void);
extern void write_u8_ff(void);
extern void write_u16_00ff(void);
#define MCS251_CHECKPOINTS() do { \
    u8g = 0xFF; UART_PUTC(0x72); { u8 v = read_u8(); harness_hex8(v); } \
    u16g = 0xFF00; UART_PUTC(0x48); { u8 v = read_u16_hi(); harness_hex8(v); } \
    write_u8_ff(); UART_PUTC(0x77); harness_hex8(u8g); \
    write_u16_00ff(); UART_PUTC(0x57); harness_hex16(u16g); \
    UART_PUTC(0x2E); \
} while (0)
#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
