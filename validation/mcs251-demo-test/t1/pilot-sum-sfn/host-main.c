/*
 * Oracle-A host driver for pilot-sum-sfn.
 * Checkpoint stream: 'B' + <tag><HEX8>* + "PASS\n".
 */
#include <stdio.h>
#include <string.h>

#include "kernel.c"

u8 g_sfn_dir[11];      /* kernel state lives in the driver (see kernel.c) */

static void ck(char tag, u8 v)
{
    putchar(tag);
    printf("%02X", v);
}

static void run(const u8 *vec, char tag)
{
    memcpy(g_sfn_dir, vec, 11);
    ck(tag, sum_sfn());
}

int main(void)
{
    /* Real 8.3 SFN shapes (11 bytes each, space padded) plus edge bytes. */
    static const u8 t1[11] = {'F','I','L','E',' ',' ',' ',' ',' ',' ',' '};
    static const u8 t2[11] = {'T','E','S','T',' ',' ',' ','T','X','T',' '};
    static const u8 t3[11] = {'A','A','A','A','A','A','A','A','A','A','A'};
    static const u8 t4[11] = {0x80,0x01,0xFF,0x7F,0x00,0x55,0xAA,0x33,0xCC,0x0F,0xF0};
    static const u8 t5[11] = {0,0,0,0,0,0,0,0,0,0,0};
    static const u8 t6[11] = {'L','O','N','G','F','I','L','E','N','A','M'};

    putchar('B');
    run(t1, 'a');
    run(t2, 'b');
    run(t3, 'c');
    run(t4, 'd');
    run(t5, 'e');
    run(t6, 'f');
    puts("PASS");
    return 0;
}
