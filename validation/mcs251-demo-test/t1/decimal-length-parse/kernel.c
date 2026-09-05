/* Extracted T1 kernel: demo-18 decimal payload length parser. */
typedef unsigned char u8;
typedef unsigned short u16;
#if defined(__SDCC_mcs251)
typedef unsigned long u32;
#else
typedef unsigned int u32;
#endif

static u8 hex_nibble(u8 c)
{
    if (c >= (u8)'0' && c <= (u8)'9') return (u8)(c - (u8)'0');
    if (c >= (u8)'A' && c <= (u8)'F') return (u8)(c - (u8)'A' + 10u);
    return 0xffu;
}

/* Native global UsbOutBuffer/RX1_Buffer is supplied as one pointer. */
u8 parse_decimal_length(const u8 *input, u8 input_count)
{
    u8 i;
    u8 length = 0;
    u8 digit;
    for (i = 11; i < input_count; i++) {
        digit = hex_nibble(input[i]);
        if (digit >= 10u) break;
        length = (u8)(length * 10u + digit);
    }
    return length;
}
