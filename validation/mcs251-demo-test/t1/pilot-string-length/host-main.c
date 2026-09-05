/*
 * Oracle-A host driver for pilot-string-length.
 * Checkpoint stream: 'B' + <tag><HEX8>* + "PASS\n".
 */
#include <stdio.h>
#include <string.h>

char g_str_buf[64];    /* kernel state lives in the driver (see kernel.c) */

#include "kernel.c"

static void ck(char tag, u8 v)
{
    putchar(tag);
    printf("%02X", v);
}

static void fill(const char *s)          /* copy + NUL into kernel input */
{
    memset(g_str_buf, 0, sizeof(g_str_buf));
    strcpy(g_str_buf, s);
}

int main(void)
{
    /* e..g exercise the loop bound: 28/29 chars terminate in-loop, exactly
       30 non-NUL chars fall out of the loop and return 0xff. */
    fill("");
    putchar('B');
    ck('a', String_length());
    fill("1");
    ck('b', String_length());
    fill("hi");
    ck('c', String_length());
    fill("hello, world");
    ck('d', String_length());
    fill("aaaaaaaaaaaaaaaaaaaaaaaaaaaa");     /* 28 */
    ck('e', String_length());
    fill("bbbbbbbbbbbbbbbbbbbbbbbbbbbbb");    /* 29 */
    ck('f', String_length());
    memset(g_str_buf, 'c', 31);               /* 31 non-NUL, no terminator */
    ck('g', String_length());
    fill("1234567890123456789012345678 9");   /* 30 chars */
    ck('h', String_length());
    puts("PASS");
    return 0;
}
