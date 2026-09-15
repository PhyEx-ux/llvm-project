/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

/*************  功能说明    **************

本例程基于STC32G144K246为主控芯片的实验箱进行编写测试。

使用Keil C251编译器，Memory Model推荐设置XSmall模式，默认定义变量在edata，单时钟存取访问速度快。

edata建议保留1K给堆栈使用，空间不够时可将大数组、不常用变量加xdata关键字定义到xdata空间。

CAN总线收发测试用例，支持CAN2.0模式与CAN-FD模式。

CAN1脚位选择，P7.0,P7.1；CAN2脚位选择，P7.2,P7.3.

CAN2.0模式，传输上限8字节数据，最高波特率1Mbps, 用户可自行修改.

CANFD模式，传输上限64字节数据，例子设置仲裁域1Mbps，数据域5Mbps, 用户可自行修改.

CAN1每秒钟发送一帧数据，CAN2收到后通过串口输出.

串口默认使用UART1(P3.0,P3.1): 115200,N,8,1.

下载时, 选择IRC频率 48MHz，主时钟使用 PLL 80MHz 时钟频率.

******************************************/
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef S1BRT
#define S1BRT MCS251_SFRBIT(0x8E, 0)
#undef S1_S0
#define S1_S0 MCS251_SFRBIT(0xA2, 6)
#undef S1_S1
#define S1_S1 MCS251_SFRBIT(0xA2, 7)
#undef S2TI
#define S2TI MCS251_SFRBIT(0x9A, 1)
#undef S2_S
#define S2_S MCS251_SFRBIT(0xBA, 0)
#undef T1x12
#define T1x12 MCS251_SFRBIT(0x8E, 6)
/* runtime printf family (validation/mcs251-runtime); fixed-arity ABI, putchar returns void */
int printf(const char *fmt, ...);
char putchar(char c);
#include <stdint.h>

#include "canfd.h"

//========================================================================

#define SEL_HPLL1       1
#define SEL_HPLL2       2
#define HPLL_SEL        SEL_HPLL2

#if(HPLL_SEL==SEL_HPLL1)
    #define MAIN_Fosc   120000000L //定义主时钟（超频）
#else
    #define MAIN_Fosc   80000000L  //定义主时钟
#endif

#define Baudrate        115200L
#define TM              (65536 -(MAIN_Fosc/Baudrate/4))
#define PrintUart       1        //1:printf 使用 UART1; 2:printf 使用 UART2

//========================================================================

uint16_t msecond;
uint8_t DataSize;
uint8_t FrameNum;

stc_can_tx_t pstcTx;
stc_can_rx_t pCan1Rx[8];
stc_can_rx_t pCan2Rx[8];

//========================================================================

void delay_ms(u16 ms);
void UartInit(void);
void HPLL_config(void);

//========================================================================
void main(void)
{
    u8 i,n;

    P_SW2 |= 0x80; //扩展寄存器(XFR)访问使能
    CKCON = 0; //提高访问XRAM速度

    P3M1 = 0x00;   P3M0 = 0x02;   //设置为准双向口，设置P3.1推挽输出
    P7M1 = 0x00;   P7M0 = 0x0a;   //设置为准双向口，设置P7.1,P7.3推挽输出

    P4M1 = 0x00;   P4M0 = 0x20;   //设置为准双向口，设置P4.5推挽输出
    
    P3PU |= 0x03;   //P30,P31内部上拉使能
    P7PU |= 0x0f;   //P70~P73内部上拉使能

    UartInit();
    HPLL_config();
    CANFD_Init(CAN1);
    CANFD_Init(CAN2);
    IE |= 0x80;

#if 0   //0: CANFD, 1: CAN2.0
    //初始化发送报文 - CAN2.0
    pstcTx.u32ID = 0xa0;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    pstcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    pstcTx.TxCtrl.tx_ctrl.FDF = 0;          //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度
#else
    //初始化发送报文 - CANFD
    pstcTx.u32ID = 0x03234567;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    pstcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    pstcTx.TxCtrl.tx_ctrl.FDF = 1;          //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 1;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_64; //DLC:数据长度
#endif

    DataSize = u8DLC2Size[pstcTx.TxCtrl.tx_ctrl.FDF][pstcTx.TxCtrl.tx_ctrl.DLC];
    for(i=0;i<DataSize;i++)
    {
        pstcTx.pu8Data[i] = 0x10+i;         //Data
    }

    printf("CANFD Test.\r\n");
    
    while(1)
    {
        delay_ms(1);

        if(++msecond >= 1000)   //1000ms到
        {
            msecond = 0;
            
            if(CANFD_READ_REG8_BIT(CAN1->CFG_STAT, 0x01) == 0) //判断有没有BUS-OFF(BUS-OFF后通过复位，或者连续收到128个11位的隐性位序列进行恢复)
            {
                //CAN_TransData(CAN1);
                
                printf("CAN1 Send ID=0x%lx.\r\n",pstcTx.u32ID);
                CAN_SendData(CAN1,&pstcTx);
                pstcTx.u32ID++;
            }
            else
            {
                printf("Bus-Off.\r\n");

                //从BUS-OFF恢复的方法
                //1.接收到总线连续128个11位隐性位序列（恢复序列）自动恢复正常，不需要特别操作

                //2.对CAN模块重新初始化
                //CANFD_Init(CAN1);     //重新初始化

                //3.对MCU进行复位
                //IAP_CONTR = 0x20;     //软件复位
                //while(1);
            }
        }

        if(B_CanRead)   //判断是否收到数据
        {
            B_CanRead = 0;
            FrameNum = CAN_ReceiveData(CAN1,pCan1Rx);  //读取数据内容
            for(i=0;i<FrameNum;i++)
            {
                DataSize = u8DLC2Size[pCan1Rx[i].RxCtrl.rx_ctrl.FDF][pCan1Rx[i].RxCtrl.rx_ctrl.DLC];
                printf("CAN1 Read%d: ID=0x%lx,DLC=%u ",i+1,pCan1Rx[i].u32ID,DataSize);
                for(n=0;n<DataSize;n++)
                {
                    printf("0x%02x ",pCan1Rx[i].pu8Data[n]);
                }
                printf("\r\n");
            }
        }

        if(B_Can2Read)   //判断是否收到数据
        {
            B_Can2Read = 0;
            FrameNum = CAN_ReceiveData(CAN2,pCan2Rx);  //读取数据内容
            for(i=0;i<FrameNum;i++)
            {
                DataSize = u8DLC2Size[pCan2Rx[i].RxCtrl.rx_ctrl.FDF][pCan2Rx[i].RxCtrl.rx_ctrl.DLC];
                printf("CAN2 Read%d: ID=0x%lx,DLC=%u ",i+1,pCan2Rx[i].u32ID,DataSize);
                for(n=0;n<DataSize;n++)
                {
                    printf("0x%02x ",pCan2Rx[i].pu8Data[n]);
                }
                printf("\r\n");
            }
        }
    }
}

//========================================================================
// 函数: void HPLL_config(void)
// 描述: PLL时钟配置函数。
// 参数: none.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-11-01
// 备注: 
//========================================================================
void HPLL_config(void)
{
#if(HPLL_SEL==SEL_HPLL2)
    WTST = 2;           //通过WTST增加等待时钟控制Flash读取速度在33MHz以内, 主时钟:480MHz/2/3=80MHz, 读Flash速度:80MHz/(1+2)=26.7MHz

    HPLLCR |= 0x80;     //使能HPLL（高速外设默认使用HPLL时钟，切换到HPLL2前要先使能HPLL，切换完再关闭HPLL）

                        //首先需要将HIRC主频调节到48MHz
    HPLL2CR &= ~0x10;   //选择HPLL2输入时钟源为HIRC
//    HPLL2CR |= 0x10;    //选择HPLL2输入时钟源为IRCM
    HPLL2PDIV = 8;      //设置HPLL2输入时钟预分频为8（HPLL输入频率必须为6MHz）
//    HPLL2CR |= 0x00;    //HPLL2=6MHz*52=312MHz
//    HPLL2CR |= 0x01;    //HPLL2=6MHz*54=324MHz
//    HPLL2CR |= 0x02;    //HPLL2=6MHz*56=336MHz
//    HPLL2CR |= 0x03;    //HPLL2=6MHz*58=348MHz
//    HPLL2CR |= 0x04;    //HPLL2=6MHz*60=360MHz
//    HPLL2CR |= 0x05;    //HPLL2=6MHz*62=372MHz
//    HPLL2CR |= 0x06;    //HPLL2=6MHz*64=384MHz
//    HPLL2CR |= 0x07;    //HPLL2=6MHz*66=396MHz
//    HPLL2CR |= 0x08;    //HPLL2=6MHz*68=408MHz
//    HPLL2CR |= 0x09;    //HPLL2=6MHz*70=420MHz
//    HPLL2CR |= 0x0a;    //HPLL2=6MHz*72=432MHz
//    HPLL2CR |= 0x0b;    //HPLL2=6MHz*74=444MHz
//    HPLL2CR |= 0x0c;    //HPLL2=6MHz*76=456MHz
//    HPLL2CR |= 0x0d;    //HPLL2=6MHz*78=468MHz
      HPLL2CR |= 0x0e;    //HPLL2=6MHz*80=480MHz
//    HPLL2CR |= 0x0f;    //HPLL2=6MHz*82=492MHz
    HPLL2CR |= 0x20;    //高速外设时钟选择 HPLL2/2
    HPLL2CR |= 0x80;    //使能HPLL2

    HPLLCR &= ~0x80;    //关闭HPLL（高速外设默认使用HPLL时钟，切换到HPLL2前要先使能HPLL，切换完再关闭HPLL）
    
    CLKDIV = 3;         //系统时钟 = 主时钟源/2 = HPLL2/2/3 = 80MHz
    CLKSEL = 0x08;      //选择HPLL2/2作为主时钟源

#else
    WTST = 3;           //通过WTST增加等待时钟控制Flash读取速度在33MHz以内, 主时钟:480MHz/2/2=120MHz, 读Flash速度:120MHz/(1+3)=30MHz
                        //首先需要将HIRC主频调节到48MHz
    HPLLCR &= ~0x10;    //选择HPLL输入时钟源为HIRC
//    HPLLCR |= 0x10;     //选择HPLL输入时钟源为IRCM
    HPLLPDIV = 8;       //设置HPLL输入时钟预分频为8（HPLL输入频率必须为6MHz）
//    HPLLCR |= 0x00;     //HPLL=6MHz*52=312MHz
//    HPLLCR |= 0x01;     //HPLL=6MHz*54=324MHz
//    HPLLCR |= 0x02;     //HPLL=6MHz*56=336MHz
//    HPLLCR |= 0x03;     //HPLL=6MHz*58=348MHz
//    HPLLCR |= 0x04;     //HPLL=6MHz*60=360MHz
//    HPLLCR |= 0x05;     //HPLL=6MHz*62=372MHz
//    HPLLCR |= 0x06;     //HPLL=6MHz*64=384MHz
//    HPLLCR |= 0x07;     //HPLL=6MHz*66=396MHz
//    HPLLCR |= 0x08;     //HPLL=6MHz*68=408MHz
//    HPLLCR |= 0x09;     //HPLL=6MHz*70=420MHz
//    HPLLCR |= 0x0a;     //HPLL=6MHz*72=432MHz
//    HPLLCR |= 0x0b;     //HPLL=6MHz*74=444MHz
//    HPLLCR |= 0x0c;     //HPLL=6MHz*76=456MHz
//    HPLLCR |= 0x0d;     //HPLL=6MHz*78=468MHz
    HPLLCR |= 0x0e;     //HPLL=6MHz*80=480MHz
//    HPLLCR |= 0x0f;     //HPLL=6MHz*82=492MHz
    HPLL2CR &= ~0x60;   //高速外设时钟选择 HPLL/2
    HPLLCR |= 0x80;     //使能HPLL

    CLKDIV = 2;         //系统时钟=主时钟源/2 = HPLL/2/2 = 120MHz
    CLKSEL = 0x04;      //选择HPLL/2作为主时钟源
#endif

//    P5M0 |= 0x10; P5M1 &= ~0x10;//设置P5.4口为推挽输出
//    P5SR &= ~0x10;              //设置P5.4口为快速模式
//    HIRCCR |= 0x10;             //输出系统时钟
//    MCLKOCR = 10;               //系统时钟 10 分频到P5.4
}

//========================================================================
// 函数: void delay_ms(unsigned int ms)
// 描述: 延时函数。
// 参数: ms,要延时的ms数.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-11-01
// 备注: 由于芯片使能了Cache功能，软件延时时间可能不太准确
//========================================================================
void delay_ms(u16 ms)
{
    u16 i;
    do{
        i = MAIN_Fosc / 6000;
        while(--i);
    }while(--ms);
}

/******************** 串口打印函数 ********************/
void UartInit(void)
{
#if(PrintUart == 1)
    S1_S1 = 0;      //UART1 switch to, 0x00: P3.0 P3.1, 0x40: P3.6 P3.7, 0x80: P1.6 P1.7, 0xC0: P4.3 P4.4
    S1_S0 = 0;

    SCON = (SCON & 0x3f) | 0x40; 
    T1x12 = 1;          //定时器时钟1T模式
    S1BRT = 0;          //串口1选择定时器1为波特率发生器
    TL1  = TM;
    TH1  = TM>>8;
    TR1 = 1;				//定时器1开始计时

//    SCON = (SCON & 0x3f) | 0x40; 
//    T2L  = TM;
//    T2H  = TM>>8;
//    AUXR |= 0x15;   //串口1选择定时器2为波特率发生器
#else
    S2_S = 0;       //UART2 switch to: 0: P1.0 P1.1,  1: P4.6 P4.7
    S2CON = (S2CON & 0x3f) | 0x40; 
    T2L  = TM;
    T2H  = TM>>8;
    AUXR |= 0x14;	      //定时器2时钟1T模式,开始计时
#endif
}

void UartPutc(unsigned char dat)
{
#if(PrintUart == 1)
    SBUF = dat; 
    while(TI==0);
    TI = 0;
#else
    S2BUF  = dat; 
    while(S2TI == 0);
    S2TI = 0;    //Clear Tx flag
#endif
}

char putchar(char c)
{
    UartPutc(c);
    return c;
}
