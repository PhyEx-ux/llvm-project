typedef struct{unsigned char operator1;unsigned char operator2;} precedence_args; extern unsigned char compare_level_packed(const precedence_args *);
#define C(t,a,b,e) do{precedence_args x;x.operator1=(a);x.operator2=(b);{u8 v=compare_level_packed(&x);UART_PUTC(t);harness_hex8(v);harness_check_u8(e,v);}}while(0)
#define MCS251_CHECKPOINTS() do{C('a','+','*',1);C('b','*','+',0);C('c','^','d',2);C('d','(',')',2);C('e','x','+',3);C('f','!','!',2);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
