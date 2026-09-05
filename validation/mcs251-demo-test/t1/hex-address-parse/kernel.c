/* Extracted T1 kernel: demo-18 hexadecimal address parser. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned long u32;
#endif

static u8 hex_nibble(u8 c)
{
    if (c >= (u8)'0' && c <= (u8)'9') return (u8)(c - (u8)'0');
    if (c >= (u8)'A' && c <= (u8)'F') return (u8)(c - (u8)'A' + 10u);
    return 0xffu;
}

/* Native global UsbOutBuffer is supplied as one pointer argument. */
u32 parse_hex_address(const u8 *input)
{
    u32 address = 0;
    u8 i;
    u8 digit;
    if (input[2] != (u8)'0' || input[3] != (u8)'X') return 0xffffffffUL;
    for (i = 4; i < 10; i++) {
        digit = hex_nibble(input[i]);
        if (digit >= 0x10u) return 0xffffffffUL;
        address = (address << 4) + digit;
    }
    return address;
}
