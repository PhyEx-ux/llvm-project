/*
 * Firmware-side wrapper for pilot-const-handle.
 * Compiled by SDCC (--c1mode) for BOTH the Oracle-B reference link and the
 * DUT (LLVM) link; only the kernel .rel differs between the two images.
 * Checkpoints print the actual value (harness_hex8) before the frozen
 * expected-value check (harness_check_u8), so the serial stream carries the
 * real kernel result for the triangle diff, not just pass/fail.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern u8 alg_const_handle(u8 c);

#define MCS251_CHECKPOINTS() do { u8 v; \
    v = alg_const_handle('e'); UART_PUTC('a'); harness_hex8(v); harness_check_u8(0x00, v); \
    v = alg_const_handle('p'); UART_PUTC('b'); harness_hex8(v); harness_check_u8(0x01, v); \
    v = alg_const_handle('x'); UART_PUTC('c'); harness_hex8(v); harness_check_u8(0x02, v); \
    v = alg_const_handle('y'); UART_PUTC('d'); harness_hex8(v); harness_check_u8(0x03, v); \
    v = alg_const_handle('z'); UART_PUTC('E'); harness_hex8(v); harness_check_u8(0x04, v); \
    v = alg_const_handle('A'); UART_PUTC('f'); harness_hex8(v); harness_check_u8(0x05, v); \
    v = alg_const_handle('q'); UART_PUTC('g'); harness_hex8(v); harness_check_u8(0x06, v); \
    v = alg_const_handle('+'); UART_PUTC('h'); harness_hex8(v); harness_check_u8(0x06, v); \
    v = alg_const_handle('0'); UART_PUTC('i'); harness_hex8(v); harness_check_u8(0x06, v); \
    v = alg_const_handle('P'); UART_PUTC('j'); harness_hex8(v); harness_check_u8(0x06, v); \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
