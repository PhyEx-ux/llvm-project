#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned int v) { putchar(t); printf("%08X", v); }
#include <string.h>
#include "kernel.c"
u8 hex_input[16];
static void run(const char*s,char t){u8 i;memset(hex_input,0,16);for(i=0;s[i]&&i<15;i++)hex_input[i]=(u8)s[i];ck32(t,parse_hex_address());}
int main(void){putchar('B');run("xx0X1234AB",'a');run("xx0X000000",'b');run("xx0XFFFFFF",'c');run("xx0X12G4AB",'d');run("xx1X1234AB",'e');puts("PASS");return 0;}
