unsigned char hex_input[16];
#ifdef SDCC_FW
typedef unsigned long parsed_u32;
#else
typedef unsigned int parsed_u32;
#endif
extern parsed_u32 parse_hex_address(void);
static const unsigned char V0[10]={'x','x','0','X','1','2','3','4','A','B'};static const unsigned char V1[10]={'x','x','0','X','0','0','0','0','0','0'};static const unsigned char V2[10]={'x','x','0','X','F','F','F','F','F','F'};static const unsigned char V3[10]={'x','x','0','X','1','2','G','4','A','B'};static const unsigned char V4[10]={'x','x','1','X','1','2','3','4','A','B'};
#define V(v,t,e) do{u8 i;parsed_u32 x;for(i=0;i<10;i++)hex_input[i]=(v)[i];x=parse_hex_address();UART_PUTC(t);harness_hex32(x);harness_check_u32(e,x);}while(0)
#define MCS251_CHECKPOINTS() do{V(V0,'a',0x1234ABUL);V(V1,'b',0);V(V2,'c',0xFFFFFFUL);V(V3,'d',0xFFFFFFFFUL);V(V4,'e',0xFFFFFFFFUL);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"
