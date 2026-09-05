unsigned short pulse_count,measured_width;unsigned char input_high,completed;extern void pulse_width_reset(void);extern void pulse_width_step(unsigned char);extern unsigned short pulse_width_result(void);extern unsigned char pulse_width_ready(void);
#define C16(t,e) do{u16 v=pulse_width_result();UART_PUTC(t);harness_hex16(v);harness_check_u16(e,v);}while(0)
#define C8(t,e) do{u8 v=pulse_width_ready();UART_PUTC(t);harness_hex8(v);harness_check_u8(e,v);}while(0)
#define MCS251_CHECKPOINTS() do{u8 i;pulse_width_reset();C16('a',0);C8('b',0);for(i=0;i<10;i++)pulse_width_step(0);pulse_width_step(1);C16('c',0);C8('d',0);for(i=0;i<11;i++)pulse_width_step(0);pulse_width_step(1);C16('e',11);C8('f',1);pulse_width_step(1);C16('g',11);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
