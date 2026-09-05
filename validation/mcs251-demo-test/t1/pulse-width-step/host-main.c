#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }

#include "kernel.c"
u16 pulse_count,measured_width;u8 input_high,completed;
int main(void){int i;putchar('B');pulse_width_reset();ck16('a',pulse_width_result());ck8('b',pulse_width_ready());for(i=0;i<10;i++)pulse_width_step(0);pulse_width_step(1);ck16('c',pulse_width_result());ck8('d',pulse_width_ready());for(i=0;i<11;i++)pulse_width_step(0);pulse_width_step(1);ck16('e',pulse_width_result());ck8('f',pulse_width_ready());pulse_width_step(1);ck16('g',pulse_width_result());puts("PASS");return 0;}
