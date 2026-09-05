/*
 * Generic MCS-251/QEMU validation harness.
 *
 * This file deliberately has no libc dependency.  It writes the QEMU UART
 * test port directly (SBUF == 0x99), which keeps the validation image small
 * and makes the serial transcript the only pass/fail oracle.
 *
 * The default checkpoint calls mcs251_probe().  For a real validation image,
 * define MCS251_CHECKPOINTS() before including this file and replace that
 * call with the checks for the LLVM module under test.  The helper functions
 * below print both expected and got values before stopping on a mismatch.
 */

typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* STC32/QEMU's serial sink; no UART initialization is needed in this model. */
__sfr __at (0x99) SBUF;
#define UART_PUTC(ch) do { SBUF = (u8)(ch); } while (0)

static void harness_hex8(u8 value)
{
    u8 digit;
    digit = (u8)((value >> 4) & 0x0f);
    UART_PUTC(digit < 10 ? ('0' + digit) : ('A' + digit - 10));
    digit = (u8)(value & 0x0f);
    UART_PUTC(digit < 10 ? ('0' + digit) : ('A' + digit - 10));
}

static void harness_hex16(u16 value)
{
    harness_hex8((u8)(value >> 8));
    harness_hex8((u8)value);
}

static void harness_hex32(u32 value)
{
    harness_hex8((u8)(value >> 24));
    harness_hex8((u8)(value >> 16));
    harness_hex8((u8)(value >> 8));
    harness_hex8((u8)value);
}

static void harness_fail_prefix(void)
{
    UART_PUTC('F'); UART_PUTC('A'); UART_PUTC('I'); UART_PUTC('L');
    UART_PUTC(' ');
    UART_PUTC('e'); UART_PUTC('x'); UART_PUTC('p'); UART_PUTC('e');
    UART_PUTC('c'); UART_PUTC('t'); UART_PUTC('e'); UART_PUTC('d');
    UART_PUTC('='); UART_PUTC('0'); UART_PUTC('x');
}

static void harness_fail_middle(void)
{
    UART_PUTC(' '); UART_PUTC('g'); UART_PUTC('o'); UART_PUTC('t');
    UART_PUTC('='); UART_PUTC('0'); UART_PUTC('x');
}

static void harness_fail_end(void)
{
    UART_PUTC('\n');
    for (;;) {
    }
}

void harness_check_u8(u8 expected, u8 got)
{
    if (expected != got) {
        harness_fail_prefix();
        harness_hex8(expected);
        harness_fail_middle();
        harness_hex8(got);
        harness_fail_end();
    }
}

void harness_check_u16(u16 expected, u16 got)
{
    if (expected != got) {
        harness_fail_prefix();
        harness_hex16(expected);
        harness_fail_middle();
        harness_hex16(got);
        harness_fail_end();
    }
}

void harness_check_u32(u32 expected, u32 got)
{
    if (expected != got) {
        harness_fail_prefix();
        harness_hex32(expected);
        harness_fail_middle();
        harness_hex32(got);
        harness_fail_end();
    }
}

/*
 * Checkpoint customization example:
 *
 *   #define MCS251_CHECKPOINTS() do { \
 *       harness_check_u8(0xa5, p13_ret8()); \
 *       harness_check_u16(0x1357, p13_ret16()); \
 *   } while (0)
 *   #include "harness-template.c"
 *
 * Keep each call's result in the ABI type expected by the LLVM module.  Add a
 * short UART_PUTC('x') before each check when locating a failing checkpoint.
 * SDCC emits the HOME reset jump, __main startup call, and __start__stack SSEG
 * definition while compiling this translation unit; they are intentionally not
 * duplicated in C here.  crt0.asm supplies the GSINIT0 entry they target.
 */
extern void mcs251_probe(void);
#ifndef MCS251_CHECKPOINTS
#define MCS251_CHECKPOINTS() do { mcs251_probe(); } while (0)
#endif

void main(void)
{
    UART_PUTC('B');
    MCS251_CHECKPOINTS();
    UART_PUTC('P'); UART_PUTC('A'); UART_PUTC('S'); UART_PUTC('S');
    UART_PUTC('\n');
    for (;;) {
    }
}
