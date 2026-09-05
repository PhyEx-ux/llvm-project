#include <stdio.h>
static void ck8(char t,unsigned char v){putchar(t);printf("%02X",(unsigned)v);}
static void ck16(char t,unsigned short v){putchar(t);printf("%04X",(unsigned)v);}

#include "kernel.c"
u8 dispatch_task_storage[40];
u8 dispatch_task_hook_ids[8];
u8 dispatch_task_count,ca,cb;
void dispatch_hook_a(void){ca++;}
void dispatch_hook_b(void){cb+=2;}
#define RUN(n) dispatch_task_storage[((n)<<2)+(n)]
int main(void){putchar('B');dispatch_task_count=4;RUN(0)=1;dispatch_task_hook_ids[0]=1;RUN(1)=1;dispatch_task_hook_ids[1]=0;RUN(2)=0;dispatch_task_hook_ids[2]=2;RUN(3)=1;dispatch_task_hook_ids[3]=2;ck8('a',task_dispatch_step());ck8('b',ca);ck8('c',cb);ck8('d',RUN(0));ck8('e',RUN(1));ck8('f',task_dispatch_step());RUN(2)=1;ck8('g',task_dispatch_step());ck8('h',cb);puts("PASS");return 0;}
