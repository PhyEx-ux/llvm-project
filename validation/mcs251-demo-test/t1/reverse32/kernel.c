/* Extracted T1 kernel: demo-74 four-byte endian conversion. */
typedef unsigned char u8;
typedef unsigned short u16;
#ifdef SDCC_FW
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

u32 reverse32(u32 value)
{
    u8 *bytes;
    u8 first;
    u8 second;
    u8 third;
    u8 fourth;

    bytes = (u8 *)&value;
    first = bytes[0];
    second = bytes[1];
    third = bytes[2];
    fourth = bytes[3];
    bytes[0] = fourth;
    bytes[1] = third;
    bytes[2] = second;
    bytes[3] = first;
    return value;
}
