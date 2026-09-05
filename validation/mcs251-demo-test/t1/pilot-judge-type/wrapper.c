/*
 * Firmware-side wrapper for pilot-judge-type (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Expected values frozen from Oracle-A.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

u8 g_chCalcStatus;     /* kernel state lives in the driver (see kernel.c) */

extern u8 alg_judge_type(u8 c);

#define CK(tag, expect, arg) do { u8 v = alg_judge_type(arg); \
    UART_PUTC(tag); harness_hex8(v); harness_check_u8(expect, v); } while (0)

#define MCS251_CHECKPOINTS() do { \
    g_chCalcStatus = 0; \
    CK('a', 0x01, '+');  \
    CK('b', 0x01, 'd');  \
    CK('c', 0x00, '5');  \
    CK('d', 0x00, '.');  \
    CK('e', 0x02, 'e');  \
    CK('f', 0x02, 'p');  \
    CK('g', 0x03, 's');  \
    CK('h', 0x04, 'i');  \
    CK('i', 0x04, 'q');  \
    g_chCalcStatus = 1; \
    CK('j', 0x01, 'i');  \
    CK('k', 0x04, 'd');  \
    CK('l', 0x02, 'x');  \
    CK('m', 0x04, 's');  \
    CK('n', 0x01, '(');  \
    g_chCalcStatus = 7; \
    CK('o', 0x04, '+');  \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
