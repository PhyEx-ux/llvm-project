/*
 * Firmware-side wrapper for pilot-led8-nibble (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Faithful u16 UserCode form (baseline reset
 * 2026-09-05, post-1c59ad49f).  Expected values frozen from Oracle-A.
 * CK(index, tag, expected): checks LED8[index]; tags match the host driver.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

u8  LED8[8];           /* kernel state lives in the driver (see kernel.c) */
u16 UserCode;
u8  IR_code;
u8  B_IR_Press;

extern void led8_update(void);

#define CK(idx, tag, expect) do { u8 v = LED8[idx]; \
    UART_PUTC(tag); harness_hex8(v); harness_check_u8(expect, v); } while (0)

#define MCS251_CHECKPOINTS() do { u8 v; \
    UserCode = 0xABCD; IR_code = 0xEF; B_IR_Press = 1; \
    led8_update(); \
    CK(0, 'a', 0x0A); CK(1, 'b', 0x0B); CK(2, 'c', 0x0C); CK(3, 'd', 0x0D); \
    CK(6, 'e', 0x0E); CK(7, 'f', 0x0F); \
    v = B_IR_Press; UART_PUTC('g'); harness_hex8(v); harness_check_u8(0x00, v); \
    UserCode = 0x0000; IR_code = 0x00; B_IR_Press = 1; \
    led8_update(); \
    CK(0, 'h', 0x00); CK(3, 'i', 0x00); CK(7, 'j', 0x00); \
    UserCode = 0xFFFF; IR_code = 0x99; B_IR_Press = 1; \
    led8_update(); \
    CK(0, 'k', 0x0F); CK(7, 'l', 0x09); \
    UserCode = 0x1234; IR_code = 0x56; B_IR_Press = 1; \
    led8_update(); \
    CK(0, 'm', 0x01); CK(1, 'n', 0x02); CK(2, 'o', 0x03); CK(3, 'p', 0x04); \
    CK(6, 'q', 0x05); CK(7, 'r', 0x06); \
    LED8[0] = 0x5A; LED8[7] = 0xA5; \
    UserCode = 0x0001; IR_code = 0x02; B_IR_Press = 0; \
    led8_update(); \
    CK(0, 's', 0x5A); CK(7, 't', 0xA5); \
    v = B_IR_Press; UART_PUTC('u'); harness_hex8(v); harness_check_u8(0x00, v); \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
