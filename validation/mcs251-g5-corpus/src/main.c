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

CAN1每秒钟通过DMA发送多帧数据，CAN2通过DMA收到后串口输出接收内容.

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

#include "canfd_dma.h"
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

//========================================================================

void delay_ms(u16 ms);
void UartInit(void);
void HPLL_config(void);
void DmaRxBuffer_Decode(uint16_t fcnt, uint16_t frm, uint8_t bw, uint8_t xdata *u8DmaTxBuf);

//========================================================================
void main(void)
{
    uint8_t i;

    P_SW2 |= 0x80; //扩展寄存器(XFR)访问使能
    CKCON = 0; //提高访问XRAM速度

    P3M1 = 0x00;   P3M0 = 0x02;   //设置为准双向口，设置P3.1推挽输出
    P7M1 = 0x00;   P7M0 = 0x0a;   //设置为准双向口，设置P7.1,P7.3推挽输出
    
    P3PU |= 0x03;   //P30,P31内部上拉使能
    P7PU |= 0x0f;   //P70~P73内部上拉使能

    UartInit();
    HPLL_config();
    
    CANFD_DMA_LoadData();
    
    CANFD_Init(CAN1);
    CANFD_Init(CAN2);
    Can1_DMA_Config();
    Can2_DMA_Config();
    IE |= 0x80;

    printf("CANFD DMA Test.\r\n");
    DMA_CAN1T_Trig();       //触发 CAN1T_DMA

    while(1)
    {
        delay_ms(1);

        if(++msecond >= 1000)   //1000ms到
        {
            msecond = 0;
            
            if(CANFD_READ_REG8_BIT(CAN1->CFG_STAT, 0x01) == 0) //没有BUS-OFF(BUS-OFF后通过复位，或者连续收到128个11位的隐性位序列进行恢复)
            {
                if(Can1DmaTxFlag)   //判断上次发送是否完成
                {
                    Can1DmaTxFlag = 0;
                    //printf("CAN1T_DONE=%u,FRM_CNH=%u,FRM_CNL=%u\r\n",DMA_CAN1T_DONE,DMA_CAN1T_FCNTH,DMA_CAN1T_FCNTL);
                    DMA_CAN1T_Trig();       //触发 CAN1T_DMA
                }
            }
            else
            {
                printf("Buss-Off.\r\n");

                //从BUS-OFF恢复的方法
                //1.接收到总线连续128个11位隐性位序列（恢复序列）自动恢复正常，不需要特别操作

                //2.对CAN模块重新初始化
                //CANFD_Init(CAN1);     //重新初始化

                //3.对MCU进行复位
                //IAP_CONTR = 0x20;     //软件复位
                //while(1);
            }
        }

        if(Can1DmaRxFlag)
        {
//            printf("DMA_CAN1R_FCNTH=%u,DMA_CAN1R_FCNTL=%u\r\n",DMA_CAN1R_FCNTH,DMA_CAN1R_FCNTL);
//            printf("CAN1R_DONEH=%u,CAN1R_DONE=%u\r\n",DMA_CAN1R_DONEH,DMA_CAN1R_DONE);    //DMA_CAN1R_CR bit7写1清空DMA_CAN1R_DONE

            Can1DmaRxFlag = 0;

            printf("CAN1 Read: ");      //打印读取结果
            for(i=0;i<128;i++)
            {
                printf("%02x ",DmaRxBuffer[i]);
            }
            printf("\r\n");

            DmaRxBuffer_Decode((((u16)DMA_CAN1R_FCNTH << 8) + DMA_CAN1R_FCNTL), DMA_CAN1R_FRM, (DMA_CAN1R_CFG & 0x10), DmaRxBuffer);    //解析读取数据

            DMA_CAN1R_Trig();       //触发 CAN1R_DMA
        }

        if(Can2DmaRxFlag)
        {
            Can2DmaRxFlag = 0;

            printf("CAN2 Read: ");      //打印读取结果
            for(i=0;i<128;i++)
            {
                printf("%02x ",DmaRxBuffer[i]);
            }
            printf("\r\n");

            DmaRxBuffer_Decode((((u16)DMA_CAN2R_FCNTH << 8) + DMA_CAN2R_FCNTL), DMA_CAN2R_FRM, (DMA_CAN2R_CFG & 0x10), DmaRxBuffer);    //解析读取数据

            DMA_CAN2R_Trig();       //触发 CAN2R_DMA
        }
    }
}

uint32_t reverse4(uint32_t d)
{   
    uint32_t ret;
    
    ((uint8_t *)&ret)[0] = ((uint8_t *)&d)[3];
    ((uint8_t *)&ret)[1] = ((uint8_t *)&d)[2];
    ((uint8_t *)&ret)[2] = ((uint8_t *)&d)[1];
    ((uint8_t *)&ret)[3] = ((uint8_t *)&d)[0];

    return ret;
}

void DmaRxBuffer_Decode(uint16_t fcnt, uint16_t frm, uint8_t bw, uint8_t xdata *u8DmaTxBuf)
{
    uint16_t Offset;
    uint8_t i,u8DataSize;
    stc_can_rx_t xdata* pCanRx;

    Offset = 0; //数据存放偏移地址
    do{
        pCanRx = (stc_can_rx_t xdata *)(u8DmaTxBuf+Offset);

        pCanRx->u32ID = reverse4(pCanRx->u32ID);
        u8DataSize = u8DLC2Size[pCanRx->RxCtrl.rx_ctrl.FDF][pCanRx->RxCtrl.rx_ctrl.DLC];
        printf("Read: ID=0x%lx,DLC=%u, ",pCanRx->u32ID,u8DataSize);
        for(i=0;i<u8DataSize;i++)
        {
            printf("0x%02x ",pCanRx->pu8Data[i]);
        }
        printf("\r\n");

        if(bw)          //tx_dma_bw=1:32bit
        {
            while(i&3)  //判断已读数据长度是否4的整数倍
            {
                i++;    //32bit模式，一帧数据占据4的整数倍缓冲区空间
            }
        }

    #if(RX_ADDR_ALIGN==0)
        frm = i + 8;
    #endif
        Offset += frm;

    }while(--fcnt);
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
