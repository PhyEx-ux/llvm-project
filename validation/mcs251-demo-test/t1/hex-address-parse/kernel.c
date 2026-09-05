/* Extracted T1 kernel: demo-18 hexadecimal address parser. */
typedef unsigned char u8;
typedef unsigned short u16;
#ifdef SDCC_FW
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

/* The extracted demo read UsbOutBuffer. Keep it in a driver-owned array so
 * all three compilers use direct indexed accesses (no unqualified gptr). */
extern u8 hex_input[16];

u32 parse_hex_address(void)
{
    u32 address = 0;
    u8 i;
    u8 digit;
    if (hex_input[2] != (u8)'0' || hex_input[3] != (u8)'X') return 0xffffffffUL;
    for (i = 4; i < 10; i++) {
        digit = hex_nibble(hex_input[i]);
        if (digit >= 0x10u) return 0xffffffffUL;
        address = (address << 4) + digit;
    }
    return address;
}
