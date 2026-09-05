/* Extracted T1 kernel: demo-18 decimal payload length parser. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

static u8 hex_nibble(u8 c)
{
    if (c >= (u8)'0' && c <= (u8)'9') return (u8)(c - (u8)'0');
    if (c >= (u8)'A' && c <= (u8)'F') return (u8)(c - (u8)'A' + 10u);
    return 0xffu;
}

#define DECIMAL_INPUT_CAPACITY 64u

/* The extracted demo read UsbOutBuffer/RX1_Buffer. Keep the input in a
 * driver-owned array so all three compilers use direct indexed accesses. */
extern u8 decimal_input[64];
extern u8 decimal_input_count;

u8 parse_decimal_length(void)
{
    u8 i;
    u8 length = 0;
    u8 digit;
    for (i = 11; i < decimal_input_count; i++) {
        digit = hex_nibble(decimal_input[i]);
        if (digit >= 10u) break;
        length = (u8)((length << 3) + (length << 1) + digit);
    }
    return length;
}
