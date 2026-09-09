typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define BYTE(a) (*(volatile u8 *)(u32)(a))
#define IE BYTE(0xa8)
#define TCON BYTE(0x88)
#define TMOD BYTE(0x89)
#define TH0 BYTE(0x8c)
#define TL0 BYTE(0x8a)
#define TH1 BYTE(0x8d)
#define TL1 BYTE(0x8b)
#define IP BYTE(0xb8)
#define IPH BYTE(0xb7)
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
/* Low internal RAM, direct addressing (single-byte address). */
#define DONE BYTE(0x20)    /* bit 0 polled by the sentinel's JB loop */
#define WPHASE BYTE(0x21)  /* where the high ISR hit the low ISR window */
#define LOGP BYTE(0x22)    /* log write index */
#define SEED BYTE(0x30)
#define HITS0 BYTE(0x31)   /* Timer0 (high) entries */
#define HITS1 BYTE(0x32)   /* Timer1 (low) entries */
#define PHASE BYTE(0x33)   /* low ISR window phase */
#define RESL BYTE(0x34)    /* low ISR result computed after the nest */
#define LOG(i) BYTE(0x100 + (i))

void sentinel(void);
