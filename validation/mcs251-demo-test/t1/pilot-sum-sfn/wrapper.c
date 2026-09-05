/*
 * Firmware-side wrapper for pilot-sum-sfn (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Vectors are const arrays accessed by index only
 * (SDCC emits __gptrget for unqualified pointer walks; libc-free strict link
 * cannot resolve it -- probe recorded in RESULTS.md).
 * Expected values frozen from Oracle-A.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

u8 g_sfn_dir[11];      /* kernel state lives in the driver (see kernel.c) */

extern u8 sum_sfn(void);

/* Explicit initializers (c1mode drops string-literal init); the runner
 * pins CONST to 0xFC8000 (QEMU 0xFFxxxx data-read quirk, RESULTS.md). */
static const u8 VECS[6][11] = {
    {'F','I','L','E',' ',' ',' ',' ',' ',' ',' '},
    {'T','E','S','T',' ',' ',' ','T','X','T',' '},
    {'A','A','A','A','A','A','A','A','A','A','A'},
    {0x80,0x01,0xFF,0x7F,0x00,0x55,0xAA,0x33,0xCC,0x0F,0xF0},
    {0,0,0,0,0,0,0,0,0,0,0},
    {'L','O','N','G','F','I','L','E','N','A','M'},
};

#define CK_VEC(n, tag, expect) do { u8 j; u8 v; \
    for (j = 0; j < 11; j++) g_sfn_dir[j] = VECS[n][j]; \
    v = sum_sfn(); UART_PUTC(tag); harness_hex8(v); harness_check_u8(expect, v); \
} while (0)

#define MCS251_CHECKPOINTS() do { \
    CK_VEC(0, 'a', 0xBC); \
    CK_VEC(1, 'b', 0x00); \
    CK_VEC(2, 'c', 0x1C); \
    CK_VEC(3, 'd', 0x72); \
    CK_VEC(4, 'e', 0x00); \
    CK_VEC(5, 'f', 0x60); \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
