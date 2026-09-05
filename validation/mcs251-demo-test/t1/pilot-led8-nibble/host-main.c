/*
 * Oracle-A host driver for pilot-led8-nibble.
 * Checkpoint stream: 'B' + <tag><HEX8>* + "PASS\n".
 */
#include <stdio.h>

#include "kernel.c"

u8  LED8[8];           /* kernel state lives in the driver (see kernel.c) */
u16 UserCode;
u8  IR_code;
u8  B_IR_Press;

static void ck(char tag, u8 v)
{
    putchar(tag);
    printf("%02X", v);
}

int main(void)
{
    putchar('B');

    UserCode = 0xABCD; IR_code = 0xEF; B_IR_Press = 1;
    led8_update();
    ck('a', LED8[0]); ck('b', LED8[1]); ck('c', LED8[2]); ck('d', LED8[3]);
    ck('e', LED8[6]); ck('f', LED8[7]); ck('g', B_IR_Press);

    UserCode = 0x0000; IR_code = 0x00; B_IR_Press = 1;
    led8_update();
    ck('h', LED8[0]); ck('i', LED8[3]); ck('j', LED8[7]);

    UserCode = 0xFFFF; IR_code = 0x99; B_IR_Press = 1;
    led8_update();
    ck('k', LED8[0]); ck('l', LED8[7]);

    UserCode = 0x1234; IR_code = 0x56; B_IR_Press = 1;
    led8_update();
    ck('m', LED8[0]); ck('n', LED8[1]); ck('o', LED8[2]); ck('p', LED8[3]);
    ck('q', LED8[6]); ck('r', LED8[7]);

    /* no-op path: flag clear, buffer must not change */
    LED8[0] = 0x5A; LED8[7] = 0xA5;
    UserCode = 0x0001; IR_code = 0x02; B_IR_Press = 0;
    led8_update();
    ck('s', LED8[0]); ck('t', LED8[7]); ck('u', B_IR_Press);

    puts("PASS");
    return 0;
}
