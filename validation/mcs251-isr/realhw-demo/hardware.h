typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define BYTE(a) (*(volatile u8 *)(u32)(a))
#define IE BYTE(0xa8)
#define TCON BYTE(0x88)
#define SCON BYTE(0x98)
#define SBUF BYTE(0x99)
#define DONE BYTE(0x20)
#define SEED BYTE(0x31)
#define HITS BYTE(0x32)
#define RESULT BYTE(0x33)
#define SHARED BYTE(0x34)
void sentinel(void);
u8 helper(u8 value);
