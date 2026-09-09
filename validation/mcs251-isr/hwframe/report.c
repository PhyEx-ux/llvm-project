/* Fixed EDATA/SFR addresses use the project's compatibility memory contract. */
typedef unsigned char u8;
typedef unsigned short u16;
#define BYTE(a) (*(volatile u8 *)(a))
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
extern void hwf_measure(void);

static void uart_init(void)
{
    BYTE(0xa8) = 0;
    BYTE(0xa2) = 0;
    BYTE(0xb1) &= 0xfc;
    BYTE(0xb2) &= 0xfc;
    BYTE(0x8e) &= 0xe7;
    SCON = 0x50;
    BYTE(0xd7) = 0xcc;
    BYTE(0xd6) = 0xff;
    BYTE(0x8e) = 0x15;
}

static void putc(u8 c)
{
    SCON &= 0xfd;
    SBUF = c;
#ifndef HWF_QEMU
    while (!(SCON & 2)) { }
#endif
    SCON &= 0xfd;
}

static void puts(const char *s)
{
    while (*s) putc((u8)*s++);
}

static void hex8(u8 v)
{
    u8 h = v >> 4;
    putc(h < 10 ? h + '0' : h - 10 + 'A');
    h = v & 15;
    putc(h < 10 ? h + '0' : h - 10 + 'A');
}

static void hex16(u16 v)
{
    hex8((u8)(v >> 8));
    hex8((u8)v);
}

static void pause_report(void)
{
#ifndef HWF_QEMU
    volatile u16 n = 50000;
    while (--n) { }
#endif
}

static void report(void)
{
    u16 before = ((u16)BYTE(0x41) << 8) | BYTE(0x40);
    u16 inside = ((u16)BYTE(0x45) << 8) | BYTE(0x44);
    u16 after = ((u16)BYTE(0x49) << 8) | BYTE(0x48);
    puts("HWF2 STATUS=");
    if (!BYTE(0x4c)) puts("TIMEOUT");
    else if (before != after) puts("SP_MISMATCH");
    else puts("CAPTURED");
    puts(" B="); hex16(before);
    puts(" I="); hex16(inside);
    puts(" R="); hex16(after);
    puts(" N=");
    if (BYTE(0x4c)) hex16((u16)(inside - before));
    else puts("----");
    puts(" PSW1="); hex8(BYTE(0x42)); putc('/');
    hex8(BYTE(0x46)); putc('/'); hex8(BYTE(0x4a));
    puts(" BE_RAW="); hex8(BYTE(0x43)); putc('/');
    hex8(BYTE(0x47)); putc('/'); hex8(BYTE(0x4b));
    puts("\r\nW@01F0=");
    if (BYTE(0x4c)) {
        for (u16 i = 0; i != 32; ++i) {
            hex8(BYTE(0x400 + i));
            if (i != 31) putc(' ');
        }
    } else puts("UNAVAILABLE");
    puts("\r\n");
}

int main(void)
{
    uart_init();
    for (;;) {
        puts("HWF2 READY 24MHz 115200; send g\r\n");
        pause_report();
        if (SCON & 1) {
            u8 c = SBUF;
            SCON &= 0xfe;
            if (c == 'g') break;
        }
    }
    puts("HWF2 RUN\r\n");
    hwf_measure();
#ifndef HWF_QEMU
    if (BYTE(0x4c)) {
        while (!(SCON & 2)) { }
    }
#endif
    puts(" RETURN\r\n");
    for (;;) {
        report();
        pause_report();
    }
}
