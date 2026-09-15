/* ---------------------------------------------------------------------------
 * TFPU status / completion probe for STC32G12K128 -- UART front end.
 *
 * ORDERING CONTRACT (mandatory, user requirement):
 *   UART is initialised and the banner is emitted BEFORE any TFPU action.
 *   Every TFPU step is bracketed by "STAGE <name>" / "STAGE <name>-ok" so a
 *   hang can be localised from the last line of the log alone.  The TFPU
 *   clock select (0x3E), 0x31 init and 0x32 clear-exception are issued only
 *   after the first serial output.
 *
 * The probe core lives in tfpu-probe.asm; this file only drives UART,
 * prints, and sequences the stages.
 * ------------------------------------------------------------------------- */
typedef unsigned char u8;
typedef unsigned short u16;

#define BYTE(a) (*(volatile u8 *)(a))

/* SFRs (direct addresses; the compatibility memory contract is used) */
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
#define AUXR BYTE(0x8e)
#define T2L  BYTE(0xd7)
#define T2H  BYTE(0xd6)
#define P_SW1 BYTE(0xa2)
#define P_SW2 BYTE(0xba)

/* Observation slots written by the assembly probe. */
#define S_R7RAW BYTE(0x30)
#define S_R7    BYTE(0x31)
#define S_EF    BYTE(0x32)
#define S_FF    BYTE(0x33)
#define S_A     BYTE(0x34)
#define S_SEL   BYTE(0x35)
#define S_L4    BYTE(0x36)
#define S_L5    BYTE(0x37)
#define S_L6    BYTE(0x38)
#define S_L7    BYTE(0x39)
#define S_MARK  BYTE(0x3a)

/* Sled geometry, mirrored from gen-sled.py (which defines TPU_SLED and
   TPU_SLED_LO for the assembler).  The dispatcher jumps to
   PC = sled + (TPU_SLED - n), so:
     n <= 255 : DPTR = sled + TPU_SLED_LO, A = 255 - n
     n >  255 : DPTR = sled,                A = TPU_SLED - n   */
#define TPU_SLED 272

/* Arm the dispatcher for delay n.  Called BEFORE the probe entry point: the
   assembly trigger sits immediately before JMP @A+DPTR. */
static void set_n(u16 n)
{
    if (n <= 255) {
        S_A = (u8)(255 - n);
        S_SEL = 0;
    } else {
        S_A = (u8)(TPU_SLED - n);
        S_SEL = 1;
    }
}

/* Probe entry points (assembly). */
extern void tpu_clk_sys(void);
extern void tpu_init(void);
extern void tpu_clr_exc(void);
extern void tpu_raw(void);
extern void tpu_read_state(void);
extern void tpu_mul_once(void);
extern void tpu_mul_at_n(void);
extern void tpu_add_at_n(void);
extern void tpu_sin_at_n(void);

static void uart_init(void)
{
    P_SW1 = 0;                 /* UART1 on P3.0/P3.1 */
    P_SW2 = 0;                 /* no XFR access needed by this probe */
    BYTE(0xb1) &= 0xfc;        /* P3M1 */
    BYTE(0xb2) &= 0xfc;        /* P3M0: P3.0/P3.1 quasi-bidirectional */
    AUXR &= 0xe7;              /* Timer2 as baud generator */
    SCON = 0x50;               /* mode 1, 8N1, REN=1 */
    T2L = 0xcc;                /* 24 MHz / 115200 with 1T Timer2 */
    T2H = 0xff;
    AUXR = 0x15;               /* T2 1T mode + T2 as UART1 baud clock */
}

static void putc(u8 c)
{
    SCON &= 0xfd;              /* TI = 0 */
    SBUF = c;
#ifndef TPU_QEMU
    while (!(SCON & 2)) { }    /* wait for TI */
#endif
    SCON &= 0xfd;
}

static void puts(const char *s)
{
    while (*s) putc((u8)*s++);
}

static void hex8(u8 v)
{
    u8 h = (u8)(v >> 4);
    putc(h < 10 ? (u8)(h + '0') : (u8)(h - 10 + 'A'));
    h = (u8)(v & 15);
    putc(h < 10 ? (u8)(h + '0') : (u8)(h - 10 + 'A'));
}

/* 4 hex digits; the scan index goes up to 272 (0x110) so 2 digits are not
   enough.  Fixed width keeps the columns aligned for easy diffing. */
static void hex16(u16 v)
{
    hex8((u8)(v >> 8));
    hex8((u8)v);
}

/* One observation line: "<tag> R7=xx EF=xx FF=xx". */
static void line3(const char *tag)
{
    puts(tag);
    puts(" R7="); hex8(S_R7);
    puts(" EF="); hex8(S_EF);
    puts(" FF="); hex8(S_FF);
    puts("\r\n");
}

static void line_long(const char *tag)
{
    puts(tag);
    puts(" R4="); hex8(S_L4);
    puts(" R5="); hex8(S_L5);
    puts(" R6="); hex8(S_L6);
    puts(" R7="); hex8(S_L7);
    puts("\r\n");
}

static void delay(void)
{
#ifndef TPU_QEMU
    volatile u16 n = 40000;
    while (--n) { }
#endif
}

/* One scan step: arm the delay, run the probe, print R4..R7.
   The compatibility ABI rejects function-pointer parameters ("static pointer
   parameters are not supported"), so each command gets its own loop rather
   than a shared one taking a callback. */
#define SCAN_STEP(name, fn)                        \
    do {                                           \
        set_n((u16)(n));                           \
        S_MARK = 0;                                \
        puts("SCAN " name " N=");                  \
        hex16((u16)(n));                           \
        putc(' ');                                 \
        (fn)();                                    \
        hex8(S_L4); putc(' ');                     \
        hex8(S_L5); putc(' ');                     \
        hex8(S_L6); putc(' ');                     \
        hex8(S_L7);                                \
        puts(" MK=");                              \
        hex8(S_MARK);                              \
        puts("\r\n");                              \
    } while (0)

static void scan_mul(void)
{
    u16 n;
    puts("SCAN mul n=0..40\r\n");
    for (n = 0; n <= 40; ++n) SCAN_STEP("mul", tpu_mul_at_n);
}

static void scan_add(void)
{
    u16 n;
    puts("SCAN add n=0..48\r\n");
    for (n = 0; n <= 48; ++n) SCAN_STEP("add", tpu_add_at_n);
}

static void scan_sin(void)
{
    u16 n;
    puts("SCAN sin n=0..272\r\n");
    for (n = 0; n <= 272; ++n) SCAN_STEP("sin", tpu_sin_at_n);
}

/* Expected results, for the reader's benefit in the log itself. */
static void expectations(void)
{
    puts("EXPECT mul  3.9*5.1 -> 419F1EB8\r\n");
    puts("EXPECT add  3.9+5.1 -> 41100000\r\n");
    puts("EXPECT sin  1.0rad  -> 3F576AA4\r\n");
    puts("NOTE   3.9f=4079999A 5.1f=40A33333 1.0f=3F800000\r\n");
    puts("NOTE   if R4..R7 stay == operand, the TFPU did not execute\r\n");
}

static void run_round(void)
{
    /* Stage 0: no TFPU command at all -- untouched register state. */
    puts("STAGE raw\r\n");
    tpu_raw();
    puts("RAW R7="); hex8(S_R7RAW);
    puts(" EF="); hex8(S_EF);
    puts(" FF="); hex8(S_FF);
    puts(" MK="); hex8(S_MARK);
    puts("\r\n");

    /* Stage 1: 0x3E select system clock. */
    puts("STAGE clk-sel\r\n");
    tpu_clk_sys();
    puts("STAGE clk-sel-ok\r\n");

    /* Stage 2: 0x31 initialise (documented to raise an exception state). */
    puts("STAGE init\r\n");
    tpu_init();
    puts("STAGE init-ok\r\n");
    tpu_read_state();
    line3("INIT");

    /* Stage 3: 0x32 clear exceptions. */
    puts("STAGE clr-exc\r\n");
    tpu_clr_exc();
    puts("STAGE clr-exc-ok\r\n");
    tpu_read_state();
    line3("CLEAR");

    /* Stage 4: one multiply, then status after the product was read. */
    puts("STAGE mul-once\r\n");
    tpu_mul_once();
    line_long("MULONCE");
    line3("POSTMUL");

    /* Stage 5: latency scans. */
    puts("STAGE scan-mul\r\n");
    scan_mul();
    puts("STAGE scan-add\r\n");
    scan_add();
    puts("STAGE scan-sin\r\n");
    scan_sin();

    puts("DONE\r\n");
    expectations();
}

static void report(void)
{
    puts("TPU1 RAW  R7="); hex8(S_R7RAW);
    puts(" EF="); hex8(S_EF); puts(" FF="); hex8(S_FF); puts("\r\n");
    puts("TPU1 ST33 R7="); hex8(S_R7);
    puts(" EF="); hex8(S_EF); puts(" FF="); hex8(S_FF); puts("\r\n");
    line_long("TPU1 LAST");
}

int main(void)
{
    u8 c;

    /* MANDATORY ORDER: UART first, then the banner, and only afterwards any
       TFPU command.  Do not move the TFPU calls above this banner. */
    uart_init();
    puts("TPU-PROBE READY 24MHz 115200; send g\r\n");
    puts("TPU-PROBE stages: raw clk-sel init clr-exc mul-once scan-mul/add/sin\r\n");

    for (;;) {
        delay();
        if (SCON & 1) {
            c = SBUF;
            SCON &= 0xfe;
            if (c == 'g') {
                puts("GO\r\n");
                run_round();
            }
        }
    }
    /* not reached */
    report();
    return 0;
}
