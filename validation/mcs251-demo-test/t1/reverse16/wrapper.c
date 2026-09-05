extern unsigned short reverse16(unsigned short);
#define C(t,e,a) do{u16 v=reverse16(a);UART_PUTC(t);harness_hex16(v);harness_check_u16(e,v);}while(0)
#define MCS251_CHECKPOINTS() do{C('a',0,0);C('b',0x3412,0x1234);C('c',0xCDAB,0xABCD);C('d',0xFF00,0xFF);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
