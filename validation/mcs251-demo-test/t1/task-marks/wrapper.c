typedef struct{unsigned short time_count;unsigned short retry_time;unsigned char run;}task_mark;task_mark mark_tasks[8];unsigned char mark_task_count;extern unsigned char task_marks_step(void);
#define C8(t,e,g) do{unsigned char v=(g);UART_PUTC(t);harness_hex8(v);harness_check_u8((e),v);}while(0)
#define C16(t,e,g) do{unsigned short v=(g);UART_PUTC(t);harness_hex16(v);harness_check_u16((e),v);}while(0)
#define MCS251_CHECKPOINTS() do{mark_task_count=3;mark_tasks[0].time_count=1;mark_tasks[0].retry_time=5;mark_tasks[0].run=0;mark_tasks[1].time_count=3;mark_tasks[1].retry_time=2;mark_tasks[1].run=0;mark_tasks[2].time_count=0;mark_tasks[2].retry_time=1;mark_tasks[2].run=7;C8('a',0x01,task_marks_step());C16('b',0x0005,mark_tasks[0].time_count);C8('c',0x01,mark_tasks[0].run);C8('d',0x00,task_marks_step());C16('e',0x0001,mark_tasks[1].time_count);C8('f',0x00,mark_tasks[1].run);C8('g',0x01,task_marks_step());C16('h',0x0002,mark_tasks[1].time_count);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
