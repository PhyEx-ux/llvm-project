/*
 * Firmware-side wrapper for pilot-string-length (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Fills the kernel's global input buffer from
 * const vectors (array indexing only -- SDCC emits __gptrget for unqualified
 * pointer walks, which the strict link cannot resolve; probe recorded in
 * RESULTS.md), then checks the walk result.
 * Expected values frozen from Oracle-A.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

char g_str_buf[64];    /* kernel state lives in the driver (see kernel.c) */

extern u8 String_length(void);

/* Vectors use EXPLICIT initializers: SDCC --c1mode drops string-literal
 * initialization of const arrays to all-zero data (probe 2026-09-05,
 * RESULTS.md); numeric/char initializers emit correctly.  The runner pins
 * the CONST area to 0xFC8000 because QEMU returns 0 for data reads from
 * the 0xFFxxxx window where CONST parks by default (probe, RESULTS.md).
 * LEN includes the terminator. */
static const char V0[1]  = {0};
static const char V1[2]  = {'1', 0};
static const char V2[3]  = {'h', 'i', 0};
static const char V3[13] = {'h','e','l','l','o',',',' ','w','o','r','l','d',0};
static const char V4[29] = {'a','a','a','a','a','a','a','a','a','a','a','a','a','a',
                            'a','a','a','a','a','a','a','a','a','a','a','a','a','a',0};
static const char V5[30] = {'b','b','b','b','b','b','b','b','b','b','b','b','b','b',
                            'b','b','b','b','b','b','b','b','b','b','b','b','b','b','b',0};
static const char V6[30] = {'c','c','c','c','c','c','c','c','c','c','c','c','c','c',
                            'c','c','c','c','c','c','c','c','c','c','c','c','c','c','c','c'};
static const char V7[30] = {'1','2','3','4','5','6','7','8','9','0','1','2','3','4',
                            '5','6','7','8','9','0','1','2','3','4','5','6','7','8',' ','9'};

#define CK_VEC(vec, len, tag, expect) do { u8 j; u8 v; \
    for (j = 0; j < (len); j++) g_str_buf[j] = vec[j]; \
    v = String_length(); UART_PUTC(tag); harness_hex8(v); harness_check_u8(expect, v); \
} while (0)

#define MCS251_CHECKPOINTS() do { \
    CK_VEC(V0, 1,  'a', 0x00); \
    CK_VEC(V1, 2,  'b', 0x01); \
    CK_VEC(V2, 3,  'c', 0x02); \
    CK_VEC(V3, 13, 'd', 0x0C); \
    CK_VEC(V4, 29, 'e', 0x1C); \
    CK_VEC(V5, 30, 'f', 0x1D); \
    CK_VEC(V6, 30, 'g', 0xFF); \
    CK_VEC(V7, 30, 'h', 0xFF); \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
