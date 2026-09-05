#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }

#include "kernel.c"
u8 stack_data[64],stack_element[8];
int main(void){stack_args a;int i;putchar('B');a.length=0;a.capacity=2;a.element_size=3;stack_element[0]=1;stack_element[1]=2;stack_element[2]=3;ck8('a',stack_push_packed(&a));ck8('b',a.length);ck8('c',stack_data[0]);ck8('d',stack_data[2]);for(i=0;i<3;i++)stack_element[i]=(u8)(9+i);ck8('e',stack_push_packed(&a));ck8('f',a.length);ck8('g',stack_data[3]);ck8('h',stack_push_packed(&a));ck8('i',a.length);a.capacity=8;while(a.length<8)stack_push_packed(&a);ck8('j',stack_push_packed(&a));ck8('k',a.length);puts("PASS");return 0;}
