#ifdef SDCC_FW
#define MCS251_DATA __xdata
#define MCS251_CODE __code
#else
#define MCS251_DATA
#define MCS251_CODE
#endif

MCS251_DATA unsigned char wildcard_pattern[64],wildcard_name[64];
typedef struct{unsigned short skip;unsigned short recurse;unsigned short pattern_pos;unsigned short name_pos;}match_args;
extern unsigned char wildcard_match_packed(MCS251_DATA const match_args*);
static void cp(MCS251_CODE const unsigned char*p,MCS251_CODE const unsigned char*n,unsigned short s,unsigned short r,char t,unsigned char e);

static const unsigned char P0[4]={'F','O','O',0},N0[4]={'f','o','o',0};
static const unsigned char P1[4]={'F','?','O',0},N1[4]={'f','a','o',0};
static const unsigned char P2[4]={'A','*','D',0},N2[5]={'a','b','c','d',0};
static const unsigned char N3[5]={'a','b','c','e',0};
static const unsigned char P4[5]={'*','T','X','T',0},N4[11]={'m','y','f','i','l','e','.','t','x','t',0};
static const unsigned char P5[4]={'A','B','C',0},N5[5]={'X','A','B','C',0};

#define MCS251_CHECKPOINTS() do{cp(P0,N0,0,8,'a',1);cp(P1,N1,0,8,'b',1);cp(P2,N2,0,8,'c',1);cp(P2,N3,0,8,'d',0);cp(P4,N4,0,12,'e',1);cp(P5,N5,1,8,'f',1);cp(P0,N0,0,0,'g',1);}while(0)

#include "/mnt/c/Prj/LLVM/MCS251/validation/mcs251-firmware/harness-template.c"

static void cp(MCS251_CODE const unsigned char*p,MCS251_CODE const unsigned char*n,unsigned short s,unsigned short r,char t,unsigned char e)
{
    unsigned char i,v;
    MCS251_DATA match_args a;
    for(i=0;i<64;i++){wildcard_pattern[i]=0;wildcard_name[i]=0;}
    for(i=0;p[i]&&i<63;i++)wildcard_pattern[i]=p[i];
    for(i=0;n[i]&&i<63;i++)wildcard_name[i]=n[i];
    a.skip=s;a.recurse=r;a.pattern_pos=0;a.name_pos=0;
    v=wildcard_match_packed(&a);
    UART_PUTC(t);harness_hex8(v);harness_check_u8(e,v);
}
