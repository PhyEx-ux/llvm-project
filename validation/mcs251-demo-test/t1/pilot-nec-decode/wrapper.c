/*
 * Firmware-side wrapper for pilot-nec-decode (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Drives the kernel with the same sample stream
 * as the host oracle; expected values frozen from Oracle-A (frame_no
 * distinguishes the two frames' expected values).
 * The per-frame loop runs ~1347 kernel calls on target; well within QEMU
 * icount budget.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

/* kernel state lives in the driver (see kernel.c) */
u8 g_ir_level;
u8 IR_SampleCnt, IR_BitCnt, IR_UserH, IR_UserL, IR_data, IR_DataShift;
u8 P_IR_RX_temp, B_IR_Sync, B_IR_Press, IR_code;
u16 UserCode;

#include "vectors.h"

extern void IR_RX_NEC(void);

#define MCS251_CHECKPOINTS() do { u16 i; u8 last_press = 0; u8 frame_no = 0; \
    for (i = 0; i < NEC_SAMPLE_COUNT; i++) { \
        g_ir_level = NEC_SAMPLES[i]; \
        IR_RX_NEC(); \
        if (B_IR_Press && !last_press) { \
            frame_no++; \
            UART_PUTC('p'); harness_hex8(B_IR_Press); harness_check_u8(0x01, B_IR_Press); \
            UART_PUTC('u'); harness_hex16(UserCode); \
            UART_PUTC('k'); harness_hex8(IR_code); \
            UART_PUTC('H'); harness_hex8(IR_UserH); \
            UART_PUTC('L'); harness_hex8(IR_UserL); \
            UART_PUTC('d'); harness_hex8(IR_data); \
            if (frame_no == 1) { \
                harness_check_u16(0x3412, UserCode); harness_check_u8(0xB5, IR_code); \
                harness_check_u8(0x34, IR_UserH); harness_check_u8(0x12, IR_UserL); \
                harness_check_u8(0xB5, IR_data); \
            } else { \
                harness_check_u16(0x00FF, UserCode); harness_check_u8(0x5A, IR_code); \
                harness_check_u8(0x00, IR_UserH); harness_check_u8(0xFF, IR_UserL); \
                harness_check_u8(0x5A, IR_data); \
            } \
            B_IR_Press = 0;   /* demo contract: app clears the flag after use */ \
        } \
        last_press = B_IR_Press; \
    } \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
