typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define BYTE(a) (*(volatile u8 *)(u32)(a))
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
#define IE BYTE(0xa8)
#define TCON BYTE(0x88)
/* Fixture interface to the hand-written assembly module (gen-v1.py). */
void v1_wr(void);
void v1_lp(void);
void v1_ix(void);
/* Combo slots: C stores the PSW/PSW1 immediates here, asm applies them.
 * C must reach these through runtime addresses (peek/poke in main.c):
 * the memory contract remaps constant direct pointers, the asm does not. */
#define V1_PSW_SLOT  0x40
#define V1_PSW1_SLOT 0x41
/* T10-identical ISR data slots, used only by the compiler arm's ISR. */
#define DONE BYTE(0x20)
#define SEED BYTE(0x31)
#define HITS BYTE(0x32)
#define RESULT BYTE(0x33)
#define SHARED BYTE(0x34)
u8 helper(u8 value);
