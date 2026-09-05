#include <stdio.h>
static void ck8(char t,unsigned char v){putchar(t);printf("%02X",(unsigned)v);}
static void ck16(char t,unsigned short v){putchar(t);printf("%04X",(unsigned)v);}

#include "kernel.c"
int main(void){putchar('B');ck16('a',temperature_lookup(4096));ck16('b',temperature_lookup(1));ck16('c',temperature_lookup(3956));ck16('d',temperature_lookup(2048));ck16('e',temperature_lookup(154));ck16('f',temperature_lookup(3955));puts("PASS");return 0;}
