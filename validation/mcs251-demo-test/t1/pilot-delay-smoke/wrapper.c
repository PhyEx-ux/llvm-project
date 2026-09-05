/*
 * Firmware-side wrapper for pilot-delay-smoke (SDCC --c1mode; shared by the
 * Oracle-B and DUT links).  Smoke case: no oracle triangle, no host-main.c
 * (runner convention: absence of host-main.c marks a smoke case).  The only
 * assertion is reaching PASS on QEMU: delay_ms(2) must actually execute its
 * ~16000 loop iterations (loops not deleted) and return.
 */
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;

extern void delay_ms(u16 ms);

#define MCS251_CHECKPOINTS() do { \
    delay_ms(2); \
} while (0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
