#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }

#include "kernel.c"
static void run(char a,char b,char t){precedence_args x;x.operator1=(u8)a;x.operator2=(u8)b;ck8(t,compare_level_packed(&x));}
int main(void){putchar('B');run('+','*','a');run('*','+','b');run('^','d','c');run('(',')','d');run('x','+','e');run('!','!','f');puts("PASS");return 0;}
