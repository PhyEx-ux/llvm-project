#include "hardware.h"
/* UART/report harness, same shape as the frozen T10 single-layer demo. */
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
static __attribute__((noinline)) void hex16(u16 v) { hex8((u8)(v >> 8)); hex8((u8)v); }
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
static __attribute__((noinline)) void error(u8 kind, u16 address, u8 expected, u8 actual)
{
    BYTE(0x110) = kind;
    BYTE(0x111) = (u8)(address >> 8);
    BYTE(0x112) = (u8)address;
    BYTE(0x113) = expected;
    BYTE(0x114) = actual;
}
static __attribute__((noinline)) u8 same(u8 kind, u16 address, u8 expected)
{
    u8 actual = BYTE(address);
    if (actual != expected) {
        error(kind, address, expected, actual);
        return 0;
    }
    return 1;
}
static __attribute__((noinline)) u8 run_one(u16 iteration)
{
    u8 i;

    IE = 0;
    TCON = 0;
    BYTE(0x110) = 0;
    DONE = 0;
    HITS0 = 0;
    HITS1 = 0;
    RESL = 0;
    PHASE = 0;
    WPHASE = 0;
    LOGP = 0;
    SEED = (u8)iteration;
    for (i = 0; i < 8; ++i) LOG(i) = 0;
    for (u16 j = 0; j < 128; ++j) {
        BYTE(0x580 + j) = 0xa5;
        BYTE(0x880 + j) = 0x5a;
    }
    TMOD = 0x11;      /* Timer0 and Timer1 both mode 1 (16-bit) */
    TH0 = 0xf0;
    TL0 = 0x00;       /* high ISR fires 4096 ticks after TR0 */
    TH1 = 0x80;
    TL1 = 0x00;       /* low ISR armed for 32768 ticks */
    IP = 0x0a;        /* PT1=1 -> level 1; PT0=1 -> level base */
    IPH = 0x02;       /* IPH.PT0=1 -> Timer0 level 3, Timer0 > Timer1 */
    sentinel();       /* sentinels + arm Timer1 + wait + snapshot */
    if (!same(1, 0x20, 1)) return 0;
    if (!same(2, 0x31, 1)) return 0;
    if (!same(2, 0x32, 1)) return 0;
    /* Preemption order: L1,H1,H2,L2 */
    if (!same(9, 0x100, 0xa1) || !same(9, 0x101, 0xb1) ||
        !same(9, 0x102, 0xb2) || !same(9, 0x103, 0xa2)) return 0;
    if (!same(10, 0x21, 1)) return 0; /* high ISR hit inside the window */
    if (!same(11, 0x34, (u8)(0x3c ^ SEED))) return 0;
    for (i = 0; i < 32; ++i)
        if (!same(3, 0x40 + i, (u8)(0x41 + i * 5))) return 0;
    if (!same(4, 0x60, BYTE(0x37)) || !same(4, 0x61, BYTE(0x38))) return 0;
    for (i = 0; i < 3; ++i)
        if (!same(5, 0x62 + i, BYTE(0x39 + i))) return 0;
    for (u16 j = 0; j < 128; ++j)
        if (!same(8, 0x580 + j, 0xa5) || !same(8, 0x880 + j, 0x5a)) return 0;
    return 1;
}
static __attribute__((noinline)) void report(u8 mode, u16 completed, u16 attempted)
{
    puts("V2N-v1 RESULT case="); putc(mode);
    puts(" status=");
    if (BYTE(0x110) == 1) puts("TIMEOUT");
    else if (BYTE(0x110)) puts("FAIL");
    else puts("PASS");
    puts(" completed="); hex16(completed);
    puts(" attempted="); hex16(attempted);
    puts(" code="); hex8(BYTE(0x110));
    puts(" addr="); hex8(BYTE(0x111)); hex8(BYTE(0x112));
    puts(" expected="); hex8(BYTE(0x113));
    puts(" actual="); hex8(BYTE(0x114));
    puts(" hits="); hex8(HITS1); putc('/'); hex8(HITS0);
    puts(" SP="); hex8(BYTE(0x38)); hex8(BYTE(0x37)); putc('/');
    hex8(BYTE(0x61)); hex8(BYTE(0x60));
    puts("\r\nNESTED=LOW->HIGH->LOW->MAIN WPHASE=");
    hex8(WPHASE);
    puts(" ORDER=A1,B1,B2,A2\r\n");
}
int main(void)
{
    uart_init();
    for (;;) {
        puts("V2N-v1 READY 24MHz 115200; s=single r=10000\r\n");
        pause();
        if (!(SCON & 1)) continue;
        u8 mode = SBUF;
        SCON &= 0xfe;
        if (mode != 's' && mode != 'r') continue;
        u16 target = mode == 's' ? 1 : 10000;
        u16 completed = 0, attempted = 0;
        for (u16 i = 0; i < 5; ++i) BYTE(0x110 + i) = 0;
        puts("V2N-v1 RUN case="); putc(mode); puts("\r\n");
        while (completed != target) {
            attempted = completed + 1;
            if (!run_one(attempted)) break;
            ++completed;
            if ((completed % 100) == 0) {
                puts("V2N-v1 PROGRESS completed="); hex16(completed); puts("\r\n");
            }
        }
        for (;;) {
            report(mode, completed, attempted);
            pause();
            if (SCON & 1) {
                u8 c = SBUF;
                SCON &= 0xfe;
                if (c == 'q') break;
            }
        }
    }
}
