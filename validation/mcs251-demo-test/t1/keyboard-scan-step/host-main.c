#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }
#include "kernel.c"
u8 key_state,previous_sample,hold_count,key_code,event_ready;
static void c(char t){ck8(t,key_code);ck8((char)(t+1),event_ready);}
int main(void){int i;putchar('B');keyboard_scan_reset();c('a');keyboard_scan_step(0x12);c('c');keyboard_scan_step(0x12);c('e');for(i=0;i<19;i++)keyboard_scan_step(0x12);c('g');keyboard_scan_step(0);c('i');keyboard_scan_step(0x23);keyboard_scan_step(0x23);c('k');puts("PASS");return 0;}
