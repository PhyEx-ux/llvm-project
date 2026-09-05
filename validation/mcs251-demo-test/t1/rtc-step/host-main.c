#include <stdio.h>
static void ck8(char t, unsigned char v) { putchar(t); printf("%02X", (unsigned)v); }
static void ck16(char t, unsigned short v) { putchar(t); printf("%04X", (unsigned)v); }
static void ck32(char t, unsigned long v) { putchar(t); printf("%08lX", v); }

#include "kernel.c"
int main(void){rtc_state c;putchar('B');c.hour=12;c.minute=34;c.second=55;c.millisecond=999;rtc_step(&c);ck8('a',rtc_hour(&c));ck8('b',rtc_minute(&c));ck8('c',rtc_second(&c));rtc_step(&c);ck8('d',rtc_hour(&c));ck8('e',rtc_minute(&c));ck8('f',rtc_second(&c));c.hour=23;c.minute=59;c.second=59;rtc_step(&c);ck8('g',rtc_hour(&c));ck8('h',rtc_minute(&c));ck8('i',rtc_second(&c));puts("PASS");return 0;}
