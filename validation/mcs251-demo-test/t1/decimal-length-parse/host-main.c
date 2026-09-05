#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }
#include <string.h>
#include "kernel.c"
u8 decimal_input[64];u8 decimal_input_count;
static void run(const char*s,u8 n,char t){u8 i;memset(decimal_input,0,64);for(i=0;i<n;i++)decimal_input[i]=(u8)s[i];decimal_input_count=n;ck8(t,parse_decimal_length());}
int main(void){putchar('B');run("ABCDEFGHIJK",11,'a');run("xxxxxxxxxxx12345",16,'b');run("xxxxxxxxxxx12A45",16,'c');run("xxxxxxxxxxx",11,'d');run("xxxxxxxxxxx999999999999",23,'e');puts("PASS");return 0;}
