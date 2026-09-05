typedef unsigned char u8;
u8 adc_key_state,adc_key_state1,adc_key_state2,adc_key_state3,adc_key_hold_count,adc_key_code,adc_key_event;
extern void adc_key_reset(void);extern void adc_key_step(unsigned short);
#define C(t,e1,e2) do{UART_PUTC(t);harness_hex8(adc_key_code);harness_check_u8(e1,adc_key_code);UART_PUTC((t)+1);harness_hex8(adc_key_event);harness_check_u8(e2,adc_key_event);}while(0)
#define MCS251_CHECKPOINTS() do{u8 i;adc_key_reset();C('a',0,0);adc_key_step(512);C('c',0,0);adc_key_step(512);C('e',0,0);adc_key_step(512);C('g',2,1);for(i=0;i<99;i++)adc_key_step(512);C('i',2,1);adc_key_step(1000);C('k',2,1);adc_key_reset();adc_key_step(768);adc_key_step(768);adc_key_step(768);C('m',3,1);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
