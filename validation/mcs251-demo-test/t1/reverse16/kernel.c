/* Extracted T1 kernel: demo-44 byte-order conversion. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

u16 reverse16(u16 value)
{
    return (u16)((value << 8) | (value >> 8));
}
