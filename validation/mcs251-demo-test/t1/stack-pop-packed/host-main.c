#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }

#include <string.h>
#include "kernel.c"
u8 stack_data[64],stack_output[8],stack_has_output;
int main(void){stack_pop_args a;int i;putchar('B');for(i=0;i<6;i++)stack_data[i]=(u8)(0xA0+i);a.length=2;a.element_size=3;stack_has_output=1;memset(stack_output,0,sizeof(stack_output));ck8('a',stack_pop_packed(&a));ck8('b',a.length);ck8('c',stack_output[0]);ck8('d',stack_output[2]);stack_has_output=0;ck8('e',stack_pop_packed(&a));ck8('f',a.length);ck8('g',stack_pop_packed(&a));ck8('h',a.length);puts("PASS");return 0;}
