/* Extracted T1 kernel: demo-74 four-byte endian conversion. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

u32 reverse32(u32 value)
{
    return ((value & 0x000000ffUL) << 24) |
           ((value & 0x0000ff00UL) << 8) |
           ((value & 0x00ff0000UL) >> 8) |
           ((value & 0xff000000UL) >> 24);
}
