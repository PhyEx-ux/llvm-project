#include "hardware.h"
static void putc(u8 c)
{
    SCON &= 0xfd;
    SBUF = c;
    while (!(SCON & 2)) { }
    SCON &= 0xfd;
}
static void puts(const char *s) { while (*s) putc((u8)*s++); }
static void hex8(u8 v)
{
    u8 n = v >> 4;
    putc(n < 10 ? n + '0' : n - 10 + 'A');
    n = v & 15;
    putc(n < 10 ? n + '0' : n - 10 + 'A');
}
static void hex16(u16 v) { hex8((u8)(v >> 8)); hex8((u8)v); }
static void pause(void)
{
    volatile u16 n = 50000;
    while (--n) { }
}
static void uart_init(void)
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
static void error(u8 kind, u16 address, u8 expected, u8 actual)
{
    BYTE(0x100) = kind;
    BYTE(0x101) = (u8)(address >> 8);
    BYTE(0x102) = (u8)address;
    BYTE(0x103) = expected;
    BYTE(0x104) = actual;
}
static u8 same(u8 kind, u16 address, u8 expected)
{
    u8 actual = BYTE(address);
    if (actual != expected) {
        error(kind, address, expected, actual);
        return 0;
    }
    return 1;
}
static u8 run_one(u16 iteration)
{
    IE = 0;
    TCON = 0;
    BYTE(0x100) = 0;
    DONE = 0;
    HITS = 0;
    RESULT = 0;
    SHARED = 0x12;
    SEED = (u8)iteration;
    for (u16 i = 0; i < 128; ++i) {
        BYTE(0x580 + i) = 0xa5;
        BYTE(0x880 + i) = 0x5a;
    }
    BYTE(0x89) = 1;
    BYTE(0x8c) = 0xf0;
    BYTE(0x8a) = 0;
    sentinel();
    if (!same(1, 0x20, 1)) return 0;
    if (!same(2, 0x32, 1)) return 0;
    for (u16 i = 0; i < 32; ++i)
        if (!same(3, 0x40 + i, (u8)(0x41 + i * 5))) return 0;
    if (!same(4, 0x60, BYTE(0x37)) || !same(4, 0x61, BYTE(0x38))) return 0;
    for (u16 i = 0; i < 3; ++i)
        if (!same(5, 0x62 + i, BYTE(0x39 + i))) return 0;
    if (!same(6, 0x34, 0x52)) return 0;
    u8 sum = 0;
    for (u8 i = 0; i < 8; ++i) sum = (u8)(sum + (u8)(SEED + i));
    u8 expected = (u8)((sum ^ 0x31) + (sum ^ 0x32) + (sum ^ 0x33) + (sum ^ 0x34));
    if (!same(7, 0x33, expected)) return 0;
    for (u16 i = 0; i < 128; ++i)
        if (!same(8, 0x580 + i, 0xa5) || !same(8, 0x880 + i, 0x5a)) return 0;
    return 1;
}
static void report(u8 mode, u16 completed, u16 attempted)
{
    puts("T10-G12-v1 RESULT case="); putc(mode);
    puts(" status=");
    if (BYTE(0x100) == 1) puts("TIMEOUT");
    else if (BYTE(0x100)) puts("FAIL");
    else puts("PASS");
    puts(" completed="); hex16(completed);
    puts(" attempted="); hex16(attempted);
    puts(" code="); hex8(BYTE(0x100));
    puts(" addr="); hex8(BYTE(0x101)); hex8(BYTE(0x102));
    puts(" expected="); hex8(BYTE(0x103));
    puts(" actual="); hex8(BYTE(0x104));
    puts(" hits="); hex8(HITS);
    puts(" SP="); hex8(BYTE(0x38)); hex8(BYTE(0x37)); putc('/');
    hex8(BYTE(0x61)); hex8(BYTE(0x60));
    puts(" PSW_RAW="); hex8(BYTE(0x3c)); putc('/'); hex8(BYTE(0x3e));
    puts(" PSW1_RAW="); hex8(BYTE(0x3d)); putc('/'); hex8(BYTE(0x3f));
    puts("\r\nFLAGS=NOT_TESTED NESTED=NOT_TESTED WINDOWS=NOT_TESTED\r\n");
}
int main(void)
{
    uart_init();
    for (;;) {
        puts("T10-G12-v1 READY 24MHz 115200; s=single r=10000\r\n");
        pause();
        if (!(SCON & 1)) continue;
        u8 mode = SBUF;
        SCON &= 0xfe;
        if (mode != 's' && mode != 'r') continue;
        u16 target = mode == 's' ? 1 : 10000;
        u16 completed = 0, attempted = 0;
        for (u16 i = 0; i < 5; ++i) BYTE(0x100 + i) = 0;
        puts("T10-G12-v1 RUN case="); putc(mode); puts("\r\n");
        while (completed != target) {
            attempted = completed + 1;
            if (!run_one(attempted)) break;
            ++completed;
            if ((completed % 100) == 0) {
                puts("T10-G12-v1 PROGRESS completed="); hex16(completed); puts("\r\n");
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
