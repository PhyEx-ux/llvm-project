typedef struct{unsigned char hour;unsigned char minute;unsigned char second;unsigned short millisecond;} rtc_state;extern void rtc_step(rtc_state*);extern unsigned char rtc_hour(const rtc_state*);extern unsigned char rtc_minute(const rtc_state*);extern unsigned char rtc_second(const rtc_state*);
#define C(t,e,f) do{u8 v=(f);UART_PUTC(t);harness_hex8(v);harness_check_u8(e,v);}while(0)
#define MCS251_CHECKPOINTS() do{rtc_state c;c.hour=12;c.minute=34;c.second=55;c.millisecond=999;rtc_step(&c);C('a',12,rtc_hour(&c));C('b',34,rtc_minute(&c));C('c',56,rtc_second(&c));rtc_step(&c);C('d',12,rtc_hour(&c));C('e',34,rtc_minute(&c));C('f',57,rtc_second(&c));c.hour=23;c.minute=59;c.second=59;rtc_step(&c);C('g',0,rtc_hour(&c));C('h',0,rtc_minute(&c));C('i',0,rtc_second(&c));}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
