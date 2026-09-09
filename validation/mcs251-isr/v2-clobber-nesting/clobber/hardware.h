typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define BYTE(a) (*(volatile u8 *)(u32)(a))
#define IE BYTE(0xa8)
#define TCON BYTE(0x88)
#define TMOD BYTE(0x89)
#define TH0 BYTE(0x8c)
#define TL0 BYTE(0x8a)
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
/* Low internal RAM, direct addressing (single-byte address). */
#define DONE BYTE(0x20)   /* bit 0 polled by the sentinel's JB loop */
#define SEED BYTE(0x30)   /* per-round pattern seed */
#define HITS BYTE(0x31)   /* ISR entry counter, must be 1 per round */
#define RESULT BYTE(0x32) /* clobber_hi() self-check error count */
#define SHARED BYTE(0x33) /* proof the whole ISR body ran */
#define DPXL BYTE(0x84)

/* Hand-written assembly module (gen-clobber.py):
 *   sentinel() -- sets R0-R31 sentinels, arms Timer0, waits flag-neutrally,
 *                 snapshots everything after the interrupt returns.
 *   clobber_hi -- ABI-legal active clobber of R16-R31, DPXL and PSW. */
void sentinel(void);
u8 clobber_hi(u8 seed);
