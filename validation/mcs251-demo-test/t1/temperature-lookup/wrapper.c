extern unsigned short temperature_lookup(unsigned short);
#define C(t,e,a) do{u16 v=temperature_lookup(a);UART_PUTC(t);harness_hex16(v);harness_check_u16(e,v);}while(0)
#define MCS251_CHECKPOINTS() do{C('a',0xFFFE,4096);C('b',0xFFFF,1);C('c',0,3956);C('d',0x028A,2048);C('e',1600,154);C('f',0x0001,3955);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
