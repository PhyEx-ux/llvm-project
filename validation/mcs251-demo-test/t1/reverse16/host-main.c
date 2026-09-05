#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }

#include "kernel.c"
int main(void){putchar('B');ck16('a',reverse16(0));ck16('b',reverse16(0x1234));ck16('c',reverse16(0xABCD));ck16('d',reverse16(0x00FF));puts("PASS");return 0;}
