#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned int v) { putchar(t); printf("%08X", v); }

#include "kernel.c"
int main(void){putchar('B');ck32('a',reverse32(0));ck32('b',reverse32(0x12345678U));ck32('c',reverse32(0xAABBCCDDU));ck32('d',reverse32(0x000000FFU));puts("PASS");return 0;}
