#ifdef SDCC_FW
extern unsigned long reverse32(unsigned long);
#else
extern unsigned int reverse32(unsigned int);
#endif
#define C(t,e,a) do{unsigned long v=reverse32(a);UART_PUTC(t);harness_hex32(v);harness_check_u32(e,v);}while(0)
#define MCS251_CHECKPOINTS() do{C('a',0,0);C('b',0x78563412U,0x12345678U);C('c',0xDDCCBBAAU,0xAABBCCDDU);C('d',0xFF000000U,0xFFU);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
