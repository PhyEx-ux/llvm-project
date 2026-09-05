typedef unsigned char u8;
u8 key_state,previous_sample,hold_count,key_code,event_ready;extern void keyboard_scan_reset(void);extern void keyboard_scan_step(u8);
#define C(t,e1,e2) do{UART_PUTC(t);harness_hex8(key_code);harness_check_u8(e1,key_code);UART_PUTC((t)+1);harness_hex8(event_ready);harness_check_u8(e2,event_ready);}while(0)
#define MCS251_CHECKPOINTS() do{u8 i;keyboard_scan_reset();C('a',0,0);keyboard_scan_step(0x12);C('c',0,0);keyboard_scan_step(0x12);C('e',0x12,1);for(i=0;i<19;i++)keyboard_scan_step(0x12);C('g',0x12,1);keyboard_scan_step(0);C('i',0x12,1);keyboard_scan_step(0x23);keyboard_scan_step(0x23);C('k',0x12,1);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
