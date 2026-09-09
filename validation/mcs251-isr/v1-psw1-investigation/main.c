#include "hardware.h"
/* V1 PSW1 investigation harness.  Serial protocol:
 *   g = run the full stage x combo matrix and report one line per run
 *   r = repeat the L stage (T10-identical DJNZ wait) 128 times and print
 *       a tally of the after-PSW1 values (phase-noise evidence)
 *   q = leave the report loop, back to READY
 * All values are hex.  Nothing here asserts PASS/FAIL: the deltas ARE the
 * deliverable, interpretation is in PROTOCOL.md. */
static __attribute__((noinline)) void putc(u8 c)
{
    SCON &= 0xfd;
    SBUF = c;
    while (!(SCON & 2)) { }
    SCON &= 0xfd;
}
static __attribute__((noinline)) void puts(const char *s) { while (*s) putc((u8)*s++); }
static __attribute__((noinline)) void hex8(u8 v)
{
    u8 n = v >> 4;
    putc(n < 10 ? n + '0' : n - 10 + 'A');
    n = v & 15;
    putc(n < 10 ? n + '0' : n - 10 + 'A');
}
static __attribute__((noinline)) void pause(void)
{
    volatile u16 n = 50000;
    while (--n) { }
}
static __attribute__((noinline)) void uart_init(void)
{
    IE = 0;
    TCON = 0;
    BYTE(0xe9) = 0;
    BYTE(0xea) = 0;
    BYTE(0xa2) = 0;
    BYTE(0xb1) &= 0xfc;
    BYTE(0xb2) &= 0xfc;
    BYTE(0x8e) &= 0xe7;
    SCON = 0x50;
    BYTE(0xd7) = 0xcc;
    BYTE(0xd6) = 0xff;
    BYTE(0x8e) = 0x15;
}
/* The compatibility ABI rejects pointer parameters, so the combo pair is
 * returned through globals.  RS bits are never set (register bank 0).
 * Branch-free on purpose: branch chains make -O2 emit a jump table the
 * backend cannot select (br_jt), which silently degrades the image to -O0
 * codegen whose pointer addressing differs. */
static u8 g_psw, g_psw1;
/* Constant direct addresses below 0x80 are remapped by the memory contract
 * (0x40 becomes 0x30), so every fixture slot is reached through a runtime
 * address instead -- same pattern as the T10 harness's same()/error(). */
static __attribute__((noinline)) u8 peek(u16 a) { return *(volatile u8 *)(u32)a; }
static __attribute__((noinline)) void poke(u16 a, u8 v) { *(volatile u8 *)(u32)a = v; }
#define V1_PSW   V1_PSW_SLOT
#define V1_PSW1  V1_PSW1_SLOT
static __attribute__((noinline)) void combo(u8 i)
{
    u8 psw1, psw;
    /* 0: 00/00   1: Z(PSW1.1)   2: N(PSW1.5)   3: C via PSW1.7
     * 4: AC via PSW.6   5: F0 via PSW.5 (PSW1.5 is N!)   6: OV shared
     * 7: RS1 via PSW.4 (register-bank select; bank restored by the frame) */
    psw1 = (u8)(((i == 1u) * 0x02) | ((i == 2u) * 0x20) |
                ((i == 3u) * 0x80) | ((i == 6u) * 0x04));
    psw  = (u8)(((i == 4u) * 0x40) | ((i == 5u) * 0x20) |
                ((i == 6u) * 0x04) | ((i == 7u) * 0x08));
    g_psw1 = psw1;
    g_psw  = psw;
}
int main(void)
{
    u8 i;
    uart_init();
    for (;;) {
        puts("V1P READY 24MHz 115200; g=matrix r=tally q=quit\r\n");
        pause();
        if (!(SCON & 1)) continue;
        i = SBUF;
        SCON &= 0xfe;
        if (i == 'g') {
            puts("V1P RUN matrix\r\n");
            for (i = 0; i < 8; ++i) {
                combo(i);
                poke(V1_PSW, g_psw);
                poke(V1_PSW1, g_psw1);
                v1_wr();
                puts("V1P stage=W combo="); hex8(i);
                puts(" w="); hex8(peek(V1_PSW)); putc('/'); hex8(peek(V1_PSW1));
                puts(" rb="); hex8(BYTE(0x42)); putc('/'); hex8(BYTE(0x43));
                puts("\r\n");
            }
            for (i = 0; i < 8; ++i) {
                combo(i);
                poke(V1_PSW, g_psw);
                poke(V1_PSW1, g_psw1);
                v1_lp();
                puts("V1P stage=L combo="); hex8(i);
                puts(" w="); hex8(peek(V1_PSW)); putc('/'); hex8(peek(V1_PSW1));
                puts(" bf="); hex8(BYTE(0x44)); putc('/'); hex8(BYTE(0x45));
                puts(" af="); hex8(BYTE(0x46)); putc('/'); hex8(BYTE(0x47));
                puts("\r\n");
            }
#ifdef V1_ARM_INTERRUPT
            for (i = 0; i < 8; ++i) {
                combo(i);
                poke(V1_PSW, g_psw);
                poke(V1_PSW1, g_psw1);
                poke(0x3c, 0);
                poke(0x3d, 0);
                v1_ix();
                puts("V1P stage=I combo="); hex8(i);
                puts(" w="); hex8(peek(V1_PSW)); putc('/'); hex8(peek(V1_PSW1));
                puts(" bf="); hex8(BYTE(0x44)); putc('/'); hex8(BYTE(0x45));
                puts(" en="); hex8(peek(0x3c)); putc('/'); hex8(peek(0x3d));
                puts(" af="); hex8(BYTE(0x48)); putc('/'); hex8(BYTE(0x49));
                puts("\r\n");
            }
#endif
            puts("V1P DONE matrix\r\n");
        } else if (i == 'r') {
            u16 k;
            puts("V1P RUN tally\r\n");
            for (k = 0; k < 256; ++k) poke(0x200 + k, 0);
            for (k = 0; k < 128; ++k) {
                poke(V1_PSW, 0);
                poke(V1_PSW1, 0);
                v1_lp();
                poke(0x200 + peek(0x47), (u8)(peek(0x200 + peek(0x47)) + 1));
            }
            puts("V1P stage=L af-psw1 tally over 128 runs:\r\n");
            for (k = 0; k < 256; ++k) {
                if (peek(0x200 + k)) {
                    puts("V1P value="); hex8((u8)k);
                    puts(" count="); hex8(peek(0x200 + k));
                    puts("\r\n");
                }
            }
            puts("V1P DONE tally\r\n");
        }
    }
}
