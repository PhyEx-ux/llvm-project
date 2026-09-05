#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }
#include "kernel.c"
u8 adc_key_state,adc_key_state1,adc_key_state2,adc_key_state3,adc_key_hold_count,adc_key_code,adc_key_event;
static void c(char t){ck8(t,adc_key_code);ck8((char)(t+1),adc_key_event);}
int main(void){int i;putchar('B');adc_key_reset();c('a');adc_key_step(512);c('c');adc_key_step(512);c('e');adc_key_step(512);c('g');for(i=0;i<99;i++)adc_key_step(512);c('i');adc_key_step(1000);c('k');adc_key_reset();adc_key_step(768);adc_key_step(768);adc_key_step(768);c('m');puts("PASS");return 0;}
