#include <stdio.h>
static void ck8(char t,unsigned char v){putchar(t);printf("%02X",(unsigned)v);}
static void ck16(char t,unsigned short v){putchar(t);printf("%04X",(unsigned)v);}

#include "kernel.c"
task_mark mark_tasks[8];u8 mark_task_count;
int main(void){putchar('B');mark_task_count=3;mark_tasks[0].time_count=1;mark_tasks[0].retry_time=5;mark_tasks[0].run=0;mark_tasks[1].time_count=3;mark_tasks[1].retry_time=2;mark_tasks[1].run=0;mark_tasks[2].time_count=0;mark_tasks[2].retry_time=1;mark_tasks[2].run=7;ck8('a',task_marks_step());ck16('b',mark_tasks[0].time_count);ck8('c',mark_tasks[0].run);ck8('d',task_marks_step());ck16('e',mark_tasks[1].time_count);ck8('f',mark_tasks[1].run);ck8('g',task_marks_step());ck16('h',mark_tasks[1].time_count);puts("PASS");return 0;}
