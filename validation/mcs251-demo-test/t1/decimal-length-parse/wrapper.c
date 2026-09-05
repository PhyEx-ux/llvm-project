typedef unsigned char u8;
u8 decimal_input[64];u8 decimal_input_count;extern u8 parse_decimal_length(void);
static const u8 V0[11]={'A','B','C','D','E','F','G','H','I','J','K'};static const u8 V1[16]={'x','x','x','x','x','x','x','x','x','x','x','1','2','3','4','5'};static const u8 V2[16]={'x','x','x','x','x','x','x','x','x','x','x','1','2','A','4','5'};static const u8 V3[23]={'x','x','x','x','x','x','x','x','x','x','x','9','9','9','9','9','9','9','9','9','9','9','9'};
#define V(v,n,t,e) do{u8 i,x;for(i=0;i<(n);i++)decimal_input[i]=(v)[i];decimal_input_count=(n);x=parse_decimal_length();UART_PUTC(t);harness_hex8(x);harness_check_u8(e,x);}while(0)
#define MCS251_CHECKPOINTS() do{V(V0,11,'a',0x00);V(V1,16,'b',0x39);V(V2,16,'c',0x0C);V(V0,11,'d',0x00);V(V3,23,'e',0xFF);}while(0)


#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
