/*
 * STC32G12K128 UART1 demo firmware — IRC = 33.1776 MHz, 115200-8N1.
 *
 * Target: real STC32G12K128 (STC-ISP must be told IRC=33.1776MHz at flash
 * time; the firmware does NOT reprogram IRC while running).
 *
 * UART1: mode 1 (async 8N1), Timer2 1T as baud generator.
 *   reload = 65536 - IRC/4/BAUD = 65536 - 33177600/4/115200 = 65536 - 72 = 0xFFB8
 *   T2L @ 0xD7, T2H @ 0xD6  (STC32G layout — NOT the 8052 0xCC/0xCD!)
 *   AUXR |= 0x15 = T2R(bit4) | T2x12(bit2) | S1BRT(bit0); bit3 (T2CT) never set.
 *
 * Build chain (proven, g12-xfrrw/g12-hexdemo real-hw PASS):
 *   clang -Xclang -mcs251-memory-contract=1,1,32,8,1
 *     -> llc -mcs251-object-format=elf -> mcs251-lld -> llvm-objcopy -O ihex
 * Output: 251 SOURCE-mode encodings only.  Source mode legally A5-escapes
 * the classic low-nibble>=6 opcodes (mov a,rn / mov rn,a families); the
 * check-encoding.py gate decodes every instruction boundary to prove it.
 */
#include "stc32g-v1.h"

#define IRC_HZ          33177600u  /* Selected in STC-ISP when flashing. */
#define UART_BAUD       115200u
#define UART_RELOAD     (0x10000u - IRC_HZ / 4u / UART_BAUD)
#define UART_RELOAD_LO  0xB8u      /* T2L @ 0xD7 */
#define UART_RELOAD_HI  0xFFu      /* T2H @ 0xD6 */

#if UART_RELOAD != 0xFFB8u
#error "UART_RELOAD mismatch: IRC/baud recipe must yield 0xFFB8"
#endif

/* Same sequence as real-hw PASS g12-xfrrw.c / demo-modern uart.h. */
static void uart_init(void)
{
    P_SW1 = 0x00u;                     /* UART1: RxD=P3.0, TxD=P3.1. */
    P3M1 &= (unsigned char)~0x03u;     /* P3.0/P3.1 quasi-bidirectional. */
    P3M0 &= (unsigned char)~0x03u;
    AUXR &= (unsigned char)~0x10u;     /* T2R=0: stop Timer2 before loading. */
    AUXR &= (unsigned char)~0x08u;     /* T2CT=0: internal clock (never set bit3). */
    SCON = 0x50u;                      /* UART1 mode 1, REN=1. */
    T2L = UART_RELOAD_LO;              /* 0xB8 -> direct 0xD7 (STC32G T2L). */
    T2H = UART_RELOAD_HI;              /* 0xFF -> direct 0xD6 (STC32G T2H). */
    AUXR |= 0x01u;                     /* S1BRT=1: UART1 uses Timer2. */
    AUXR |= 0x04u;                     /* T2x12=1: 1T mode (bit2). */
    AUXR |= 0x10u;                     /* T2R=1: start last. AUXR == 0x15. */
}

/* TI-polled transmit (real-hw proven). QEMU stc32g model also sets TI on
 * SBUF write, so the same binary is exercised there. */
static void uart_putc(unsigned char value)
{
    SBUF = value;
    while ((SCON & 0x02u) == 0u) {
    }
    SCON &= (unsigned char)~0x02u;
}

static void uart_puts(const char *text)
{
    while (*text != '\0')
        uart_putc((unsigned char)*text++);
}

/* Fixed 4-digit zero-padded decimal (0..9999): exercises i32 div/mod. */
static void uart_putdec4(unsigned int value)
{
    unsigned char digits[4];
    unsigned char i;

    for (i = 0u; i < 4u; ++i) {
        digits[i] = (unsigned char)('0' + (value % 10u));
        value /= 10u;
    }
    for (i = 4u; i-- > 0u; )
        uart_putc(digits[i]);
}

/* Rough pacing so real-hw lines are readable; not calibrated. */
static void pause_between_lines(void)
{
    volatile unsigned int outer = 80u;

    while (outer-- != 0u) {
        volatile unsigned int inner = 3000u;

        while (inner-- != 0u) {
        }
    }
}

int main(void)
{
    unsigned int beats = 0u;

    uart_init();
    uart_puts("MCS251-UARTDEMO-33M-START\r\n");
    uart_puts("IRC=33177600 UART1=115200-8N1 T2RELOAD=0xFFB8 AUXR=0x15\r\n");
    for (;;) {
        uart_puts("HEARTBEAT ");
        uart_putdec4(beats);
        uart_puts("\r\n");
        if (++beats > 9999u)
            beats = 0u;
        pause_between_lines();
    }
}
