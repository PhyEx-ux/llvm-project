/* sbit -> builtin unsigned char lvalue (README 3.1) */
#define P_OLED_CLK __builtin_mcs251_bit_lvalue(0xA5)
#define P_OLED_DIN __builtin_mcs251_bit_lvalue(0xA3)
#define P_OLED_RST __builtin_mcs251_bit_lvalue(0xA4)
#define P_OLED_DC __builtin_mcs251_bit_lvalue(0xA2)
#define P_OLED_CS __builtin_mcs251_bit_lvalue(0xA6)

/*************  功能说明    **************

本例程基于STC32G144K246为主控芯片的实验箱进行编写测试。

使用Keil C251编译器，Memory Model推荐设置XSmall模式，默认定义变量在edata，单时钟存取访问速度快。

edata建议保留1K给堆栈使用，空间不够时可将大数组、不常用变量加xdata关键字定义到xdata空间。

单色OLED12864显示屏驱动程序，驱动IC为SSD1306，SPI接口，通过SPI DMA将1024字节的图片数据送到彩屏，传送时不占用CPU时间。

显示图形，汉字，英文，数字.

其中图形显示发送命令和图片数据使用SPI DMA操作，传输数据时不占用CPU时间。做GUI最方便了，可以先操作定义于xdata的1024字节缓存，然后触发SPI DMA即可，523us或943us即可自己动刷完。
本例运行于40MHz, SPI速度为主频4分频(10MHz)，每次SPI DMA传输总时间943us，SPI速度为主频2分频(20MHz)，每次SPI DMA传输总时间523us。

将要显示的内容放在1024字节的显存中，启动DMA传输即可。

下载时, 选择IRC频率 48MHz，主时钟使用 PLL 90MHz 时钟频率.

******************************************/
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef CPHA
#define CPHA MCS251_SFRBIT(0xCE, 2)
#undef CPOL
#define CPOL MCS251_SFRBIT(0xCE, 3)
#undef DORD
#define DORD MCS251_SFRBIT(0xCE, 5)
#undef MSTR
#define MSTR MCS251_SFRBIT(0xCE, 4)
#undef SPEN
#define SPEN MCS251_SFRBIT(0xCE, 6)
#undef SPIF
#define SPIF MCS251_SFRBIT(0xCD, 7)
#undef SPI_S0
#define SPI_S0 MCS251_SFRBIT(0xA2, 2)
#undef SPI_S1
#define SPI_S1 MCS251_SFRBIT(0xA2, 3)
#undef SSIG
#define SSIG MCS251_SFRBIT(0xCE, 7)

#include "ASCII6x8.h"
#include "HZK16.h"
#include "ASCII-10x24.h"
#include "picture1.h"
#include "picture2.h"

#define    u8 unsigned char
#define    u16 unsigned int
#define    u32 unsigned long

/****************  用户定义宏  ****************/

#define SEL_HPLL1       1
#define SEL_HPLL2       2
#define HPLL_SEL        SEL_HPLL2

#if(HPLL_SEL==SEL_HPLL1)
    #define MAIN_Fosc   120000000L //定义主时钟（超频）
#else
    #define MAIN_Fosc   90000000L  //定义主时钟
#endif

/****************  本地引脚定义  **************/
/*    定义接口    */
                            //GND   STC32G144K246实验箱
                            //VCC   3~5V
 //D0    SPI or II2 的时钟脚
 //D1    SPI or II2 的数据脚
 //RES   复位脚, 低电平复位
 //DC    数据或命令脚
 //CS    片选脚

/****************  本地变量声明  **************/

u16 SPI_TxCnt;      //SPI DMA触发发送次数, 一次128字节, 一共8次
unsigned char B_SPI_DMA_busy; //SPI DMA忙标志， 1标志SPI-DMA忙，SPI DMA中断中清除此标志，使用SPI DMA前要确认此标志为0
u16 SPI_TxAddr;     //SPI DMA要发送数据的首地址
unsigned char B_TxCmd;        //已发送命令标志
u8 xdata CmdTmp[5]; //命令缓冲

u8 xdata DisTmp[1024];    //显示缓冲，将要显示的内容放在显存里，启动DMA即可. 由于LCM DMA有4字节对齐问题，所以这里定位对地址为4的倍数

/****************  本地函数声明  **************/

void HPLL_config(void);

//========================================================================
// 函数: void  SPI_Config(void)
// 描述: SPI初始化函数。
// 参数: none
// 返回: none.
// 版本: VER1.0
// 日期: 2024-8-13
// 备注:
//========================================================================
void SPI_Config(void)
{
    SPI_S1 = 0;     //00: P1.2 P1.3 P1.4 P1.5, 01: P2.2 P2.3 P2.4 P2.5, 10: P5.4 P4.0 P4.1 P4.2, 11: P3.5 P3.4 P3.3 P3.2
    SPI_S0 = 1;

    SSIG = 1;       //1: 忽略SS脚，由MSTR位决定主机还是从机        0: SS脚用于决定主机还是从机。
    SPEN = 1;       //1: 允许SPI，                                0: 禁止SPI，所有SPI管脚均为普通IO
    DORD = 0;       //1: LSB先发，                                0: MSB先发
    MSTR = 1;       //1: 设为主机                                 0: 设为从机
    CPOL = 1;       //1: 空闲时SCLK为高电平，                     0: 空闲时SCLK为低电平
    CPHA = 1;       //1: 数据在SCLK前沿驱动,后沿采样.              0: 数据在SCLK前沿采样,后沿驱动.
    SPCTL = (SPCTL & ~3) | 0;   //SPI 时钟频率选择, 0: 4T, 1: 8T,  2: 16T,  3: 2T

    SPI_CLKDIV = 2; //SPI_CLKDIV时钟分频

    HSSPI_CFG = 0;
    HSSPI_CFG2 |= 0x20;     //使能SPI高速模式
    SPSTAT = 0x80 + 0x40;   //清0 SPIF和WCOL标志
}

void LCD_delay_ms(u16 ms)    // 1~65535
{
    u16 i;
    do
    {
        i = MAIN_Fosc / 6000;
        while(--i)    ;
    }while(--ms);
}

//******************************************
void OLED_WriteData(u8 dat)          //write display to LCD
{
    P_OLED_DC = 1;
    P_OLED_CS = 0;
    SPDAT = dat;    //发送一个字节
    while(SPIF == 0)    ;            //等待发送完成
    SPSTAT = 0x80 + 0x40;            //清0 SPIF和WCOL标志
    P_OLED_CS = 1;
}

//******************************************
void OLED_WriteCMD(u8 cmd)
{
    P_OLED_DC = 0;
    P_OLED_CS = 0;
    SPDAT = cmd;    //发送一个字节
    while(SPIF == 0)    ;            //等待发送完成
    SPSTAT = 0x80 + 0x40;            //清0 SPIF和WCOL标志
    P_OLED_CS = 1;
    P_OLED_DC = 1;
}

//========================================================================
// 函数: void Set_Dot_Addr_LCD(int x,int y)
// 描述: 设置在LCD的真实坐标系上的X、Y点对应的RAM地址
// 参数: x         X轴坐标
//         y         Y轴坐标
// 返回: 无
// 备注: 仅设置当前操作地址，为后面的连续操作作好准备
// 版本:
//      2007/04/10      First version
//========================================================================
void Set_Dot_Addr(u8 x,u8 y)
{
    OLED_WriteCMD((u8)(0xb0 + y));      //设置页0~7
    OLED_WriteCMD((x >> 4)  | 0x10);    //设置列0~127 高nibble
    OLED_WriteCMD(x & 0x0f);            //设置列0~127 低nibble
}

//******************************************
void FillPage(u8 y,u8 color)            //Clear Page LCD RAM
{
    u8 j;
    Set_Dot_Addr(0,y);
    for(j=0; j<128; j++)    OLED_WriteData(color);
}

//******************************************
void FillAll(u8 color)            //Clear CSn LCD RAM
{
    u8 i;
    for(i=0; i<8; i++)    FillPage(i,color);
}

//******************************************
void Initialize_OLED(void)    //initialize OLED
{
    P_OLED_CS  = 1;
    P_OLED_RST = 1;
    P_OLED_DC  = 1;
    P_OLED_DIN = 1;
    P_OLED_CLK = 1;

    P_OLED_RST = 0;
    LCD_delay_ms(100);
    P_OLED_RST = 1;
    LCD_delay_ms(100);


    OLED_WriteCMD(0xAE);     //Set Display Off

    OLED_WriteCMD(0xd5);     //display divide ratio/osc. freq. mode
    OLED_WriteCMD(0x80);     //

    OLED_WriteCMD(0xA8);     //multiplex ration mode:63
    OLED_WriteCMD(0x3F);

    OLED_WriteCMD(0xD3);     //Set Display Offset
    OLED_WriteCMD(0x00);

    OLED_WriteCMD(0x40);     //Set Display Start Line

    OLED_WriteCMD(0x8D);     //Set Display Offset
    OLED_WriteCMD(0x14);

    OLED_WriteCMD(0xA1);     //Segment Remap

    OLED_WriteCMD(0xC8);     //Sst COM Output Scan Direction

    OLED_WriteCMD(0xDA);     //common pads hardware: alternative
    OLED_WriteCMD(0x12);

    OLED_WriteCMD(0x81);     //contrast control
    OLED_WriteCMD(0xCF);

    OLED_WriteCMD(0xD9);     //set pre-charge period
    OLED_WriteCMD(0xF1);

    OLED_WriteCMD(0xDB);     //VCOM deselect level mode
    OLED_WriteCMD(0x40);     //set Vvcomh=0.83*Vcc

    OLED_WriteCMD(0xA4);     //Set Entire Display On/Off

    OLED_WriteCMD(0xA6);     //Set Normal Display

    OLED_WriteCMD(0xAF);     //Set Display On

    FillAll(0);
}

//******************************************

void WriteAscii6x8(u8 x,u8 y, u8 number)
{
    u8 const *p;
    u8 i;

    if(x > (128-5))    return;
    if(y >= 8)    return;
    p = (u16)number * 6 + ASCII6x8;

    Set_Dot_Addr(x,y);
    for(i=0; i<6; i++)
    {
        OLED_WriteData(*p);
        p++;
    }
}

//=====================================================
void WriteHZ16(u8 x, u8 y, u16 hz)    //向指定位置写一个汉字, x为横向的点0~127, y为纵向的页0~7, hz为要写的汉字.
{
    u8 const *p;
    u8 i;

    if(x > (128-16))    return;
    if(y > 6)           return;
    p = hz * 32 + HZK16;
    Set_Dot_Addr(x, y);
    for(i=0; i<16; i++)        {    OLED_WriteData(*p);    p++;}

    Set_Dot_Addr(x, (u8)(y+1));
    for(i=0; i<16; i++)        {    OLED_WriteData(*p);    p++;}
}

void printf_ASCII_text(u8 x, u8 y, u8 *ptr)    //最快写入10个ASCII码(10*6+9=69个字节)耗时430us@24MHZ
{
    u8  c;

    for (;;)
    {
        c = *ptr;
        if(c == 0)   return;    //遇到停止符0结束
        if(c < 0x80)            //ASCII码
        {
            WriteAscii6x8(x,y,c);
            x += 6;
        }
        ptr++;
    }
}

//******************************************
void WriteAscii_10x24(u8 x, u8 y, u8 chr)    //向指定位置写一个ASCII码字符, x为横向的点0~127, y为纵向的页0~7, chr为要写的字符
{
    u8 const *p;
    u8 i;

    if(x > (128-10))    return;
    if(y >= 6)          return;
    p = (u16)chr * 30 + ASCII10x24;

    Set_Dot_Addr(x, y);
    for(i=0; i<10; i++)        {    OLED_WriteData(*p);    p++;    }

    Set_Dot_Addr(x, (u8)(y+1));
    for(i=0; i<10; i++)        {    OLED_WriteData(*p);    p++;    }

    Set_Dot_Addr(x, (u8)(y+2));
    for(i=0; i<10; i++)        {    OLED_WriteData(*p);    p++;    }
}

//******************************************
void WriteDot_3x3(u8 x, u8 y)    //向指定位置写一个小数点, x为横向的点0~127, y为纵向的页0~7
{
    if(x > (128-3))    return;
    if(y >= 8)         return;

    Set_Dot_Addr(x, y);
    OLED_WriteData(0x38);
    OLED_WriteData(0x38);
    OLED_WriteData(0x38);
}

//************ 打印ASCII 10x24英文字符串 *************************
void printf_ascii_10x24(u8 x, u8 y, u8 const *ptr)    //x为横向的点0~127, y为纵向的页0~7, *ptr为要打印的字符串指针, 间隔2点.
{
    u8 c;

    for (;;)
    {
        if(x > (128-10))    return;
        if(y > 5)           return;
        c = *ptr;
        if(c == 0)        return;    //遇到停止符0结束
        if((c >= '0') && (c <= '9'))            //ASCII码
        {
            WriteAscii_10x24(x,y,(u8)(c-'0'));
            x += 12;
        }
        else if(c == '.')
        {
            WriteDot_3x3(x,(u8)(y+2));
            x += 6;
        }
        else if(c == ' ')    //显示空格
        {
            WriteAscii_10x24(x,y,11);
            x += 12;
        }
        else if(c == '-')    //显示空格
        {
            WriteAscii_10x24(x,y,10);
            x += 12;
        }
            ptr++;
    }
}

//DMA_SPI_CR     SPI_DMA控制寄存器
#define        DMA_ENSPI         (1<<7)    // SPI DMA功能使能控制位，    bit7, 0:禁止SPI DMA功能，  1：允许SPI DMA功能。
#define        SPI_TRIG_M        (1<<6)    // SPI DMA主机模式触发控制位，bit6, 0:写0无效，          1：写1开始SPI DMA主机模式操作。
#define        SPI_TRIG_S        (0<<5)    // SPI DMA从机模式触发控制位，bit5, 0:写0无效，          1：写1开始SPI DMA从机模式操作。
#define        SPI_CLRFIFO        1        // 清除SPI DMA接收FIFO控制位，bit0, 0:写0无效，          1：写1复位FIFO指针。


//DMA_SPI_CFG     SPI_DMA配置寄存器
#define        DMA_SPIIE    (1<<7)    // SPI DMA中断使能控制位，bit7, 0:禁止SPI DMA中断，     1：允许中断。
#define        SPI_ACT_TX   (1<<6)    // SPI DMA发送数据控制位，bit6, 0:禁止SPI DMA发送数据，主机只发时钟不发数据，从机也不发. 1：允许发送。
#define        SPI_ACT_RX   (0<<5)    // SPI DMA接收数据控制位，bit5, 0:禁止SPI DMA接收数据，主机只发时钟不收数据，从机也不收. 1：允许接收。
#define        DMA_SPIIP    (0<<2)    // SPI DMA中断优先级控制位，bit3~bit2, (最低)0~3(最高).
#define        DMA_SPIPTY    0        // SPI DMA数据总线访问优先级控制位，bit1~bit0, (最低)0~3(最高).

//DMA_SPI_CFG2     SPI_DMA配置寄存器2
#define        SPI_WRPSS    (0<<2)    // SPI DMA过程中使能SS脚控制位，bit2, 0: SPI DMA传输过程不自动控制SS脚。  1：自动拉低SS脚。
#define        SPI_SSS       0        // SPI DMA过程中自动控制SS脚选择位，bit1~bit0, 0: P1.4,  1：P2.4,  2: P4.0,  3:P3.5。

//DMA_SPI_STA     SPI_DMA状态寄存器
#define        SPI_TXOVW    (1<<2)    // SPI DMA数据覆盖标志位，bit2, 软件清0.
#define        SPI_RXLOSS   (1<<1)    // SPI DMA接收数据丢弃标志位，bit1, 软件清0.
#define        DMA_SPIIF     1        // SPI DMA中断请求标志位，bit0, 软件清0.

//HSSPI_CFG  高速SPI配置寄存器
#define        SS_HOLD      (0<<4)    //高速模式时SS控制信号的HOLD时间， 0~15, 默认3. 在DMA中会增加N个系统时钟，当SPI速度为系统时钟/2时执行DMA，SS_HOLD、SS_SETUP和SS_DACT都必须设置大于2的值.
#define        SS_SETUP      0        //高速模式时SS控制信号的SETUP时间，0~15, 默认3. 在DMA中不影响时间，       当SPI速度为系统时钟/2时执行DMA，SS_HOLD、SS_SETUP和SS_DACT都必须设置大于2的值.

//HSSPI_CFG2  高速SPI配置寄存器2
#define        SPI_IOSW     (0<<6)    //bit6:交换MOSI和MISO脚位，0：不交换，1：交换
#define        HSSPIEN      (0<<5)    //bit5:高速SPI使能位，0：关闭高速模式，1：使能高速模式
#define        FIFOEN       (1<<4)    //bit4:高速SPI的FIFO模式使能位，0：关闭FIFO模式，1：使能FIFO模式，使能FIFO模式在DMA中减少13个系统时间。
#define        SS_DACT       3        //bit3~0:高速模式时SS控制信号的DEACTIVE时间，0~15, 默认3, 不影响DMA时间.  当SPI速度为系统时钟/2时执行DMA，SS_HOLD、SS_SETUP和SS_DACT都必须设置大于2的值.


void SPI_DMA_TRIG(u8 xdata *TxBuf)
{
    HSSPI_CFG  = SS_HOLD | SS_SETUP;    //SS_HOLD会增加N个系统时钟, SS_SETUP没有增加时钟。驱动OLED 40MHz时SS_HOLD可以设置为0，
    HSSPI_CFG2 = SPI_IOSW | FIFOEN | SS_DACT;    //FIFOEN允许FIFO会减小13个时钟.

    SPI_TxAddr   = (u16)TxBuf;          //要发送数据的首地址
    DMA_SPI_ITVH = 0;
    DMA_SPI_ITVL = 0;
    DMA_SPI_STA  = 0;
    DMA_SPI_CFG  = DMA_SPIIE | SPI_ACT_TX | SPI_ACT_RX | DMA_SPIIP | DMA_SPIPTY;
    DMA_SPI_CFG2 = SPI_WRPSS | SPI_SSS;

    P_OLED_CS = 0;
    B_TxCmd   = 0;        //已发送命令标志
    SPI_TxCnt = 0;        //SPI DMA触发发送次数, 一次128字节, 一共8次
    B_SPI_DMA_busy = 1;   //标志SPI-DMA忙，SPI DMA中断中清除此标志，使用SPI DMA前要确认此标志为0
    DMA_SPI_STA = DMA_SPIIF;    //软件触发SPI DMA中断，启动发送
}

//========================================================================
// 函数: void SPI_DMA_ISR (void) interrupt DMA_SPI_VECTOR
// 描述:  SPI_DMA中断函数.
// 参数: none.
// 返回: none.
// 版本: V1.0, 2024-1-5
//========================================================================
void __attribute__((interrupt(49))) SPI_DMA_ISR (void)
{
    if(SPI_TxCnt >= 8)    //判断发送是否完毕.
    {
        DMA_SPI_CR = 0;            //关闭SPI DMA
        B_SPI_DMA_busy = 0;        //清除SPI-DMA忙标志，SPI DMA中断中清除此标志，使用SPI DMA前要确认此标志为0
        SPSTAT = 0x80 + 0x40;      //清0 SPIF和WCOL标志
        HSSPI_CFG2 = SPI_IOSW | SS_DACT;    //使用SPI查询或中断方式时，要禁止FIFO
        P_OLED_CS = 1;
    }
    else        //仍有数据要发送
    {
        if(!B_TxCmd)    //还没有发设置地址命令，则先发设置地址命令
        {
            B_TxCmd = 1;    //指示已发地址命令
            CmdTmp[0] = (u8)(0xb0 + SPI_TxCnt);
            CmdTmp[1] = (u8)(((0>>4) & 0x0f) + 0x10); //设置列地址的高4 位
            CmdTmp[2] = 0 & 0x0f;        //设置列地址的低4 位

            P_OLED_DC = 0;    //写命令
            DMA_SPI_TXAH = (u8)((u16)CmdTmp >> 8);    //SPI DMA发送命令首地址
            DMA_SPI_TXAL = (u8)CmdTmp;
            DMA_SPI_AMTH = 0;                //设置传输总字节数(高8位),    设置传输总字节数 = N+1
            DMA_SPI_AMT  = 3-1;              //设置传输总字节数(低8位).
            DMA_SPI_CR   = DMA_ENSPI | SPI_TRIG_M | SPI_TRIG_S | SPI_CLRFIFO;    //启动SPI DMA发送命令
        }
        else
        {
            B_TxCmd = 0;    //清除已发地址命令
            P_OLED_DC = 1;    //写数据
            DMA_SPI_TXAH = (u8)(SPI_TxAddr >> 8);    //SPI DMA发送数据首地址
            DMA_SPI_TXAL = (u8)SPI_TxAddr;
            DMA_SPI_AMTH = 0;                //设置传输总字节数(高8位),    设置传输总字节数 = N+1
            DMA_SPI_AMT  = 128-1;            //设置传输总字节数(低8位).
            DMA_SPI_CR   = DMA_ENSPI | SPI_TRIG_M | SPI_TRIG_S | SPI_CLRFIFO;    //启动SPI DMA发送命令
            SPI_TxAddr  += 128;    //要发送数据的首地址, 一次DMA传输16字节
            SPI_TxCnt++;           //发送次数+1
        }
    }

    DMA_SPI_STA = 0;        //清除中断标志
}

//====================================================================================

void main(void)
{
    u16    i;

    P_SW2 |= 0x80; //扩展寄存器(XFR)访问使能
    CKCON = 1; //避免 DMA 与 CPU 同时访问 xdata 时产生总线冲突

    P2M1 &= ~0x7c;  //SPI接口设置成推挽输出
    P2M0 |= 0x7c;
    
    P2SR &= ~0x28;  //P2.3(MOSI) P2.5(SCLK)设置成高速模式

    HPLL_config();
    SPI_Config();
    Initialize_OLED();
    IE |= 0x80;

    while(1)
    {
        for(i=0; i<1024; i++)    DisTmp[i] = 0;    //清除显存
        SPI_DMA_TRIG(DisTmp);
        while(B_SPI_DMA_busy);    //等待SPI DMA完成

        printf_ASCII_text(0, 0, "  OLED12864 SSD1306");
        for(i=0; i<8; i++)    WriteHZ16((u8)(i*16),2,i);
        printf_ascii_10x24(0,5,"-12.345 678");
        LCD_delay_ms(3000);

        for(i=0; i<1024; i++)    DisTmp[i] = gImage_picture1[i];    //将图片装载到显存
        SPI_DMA_TRIG(DisTmp);
        LCD_delay_ms(3000);

        for(i=0; i<1024; i++)    DisTmp[i] = gImage_picture2[i];    //将图片装载到显存
        SPI_DMA_TRIG(DisTmp);
        LCD_delay_ms(3000);
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
    WTST = 2;           //通过WTST增加等待时钟控制Flash读取速度在33MHz以内, 主时钟:360MHz/2/2=90MHz, 读Flash速度:90MHz/(1+2)=30MHz

    HPLLCR |= 0x80;     //使能HPLL（高速外设默认使用HPLL时钟，切换到HPLL2前要先使能HPLL，切换完再关闭HPLL）

                        //首先需要将HIRC主频调节到48MHz
    HPLL2CR &= ~0x10;   //选择HPLL2输入时钟源为HIRC
//    HPLL2CR |= 0x10;    //选择HPLL2输入时钟源为IRCM
    HPLL2PDIV = 8;      //设置HPLL2输入时钟预分频为8（HPLL输入频率必须为6MHz）
//    HPLL2CR |= 0x00;    //HPLL2=6MHz*52=312MHz
//    HPLL2CR |= 0x01;    //HPLL2=6MHz*54=324MHz
//    HPLL2CR |= 0x02;    //HPLL2=6MHz*56=336MHz
//    HPLL2CR |= 0x03;    //HPLL2=6MHz*58=348MHz
    HPLL2CR |= 0x04;    //HPLL2=6MHz*60=360MHz
//    HPLL2CR |= 0x05;    //HPLL2=6MHz*62=372MHz
//    HPLL2CR |= 0x06;    //HPLL2=6MHz*64=384MHz
//    HPLL2CR |= 0x07;    //HPLL2=6MHz*66=396MHz
//    HPLL2CR |= 0x08;    //HPLL2=6MHz*68=408MHz
//    HPLL2CR |= 0x09;    //HPLL2=6MHz*70=420MHz
//    HPLL2CR |= 0x0a;    //HPLL2=6MHz*72=432MHz
//    HPLL2CR |= 0x0b;    //HPLL2=6MHz*74=444MHz
//    HPLL2CR |= 0x0c;    //HPLL2=6MHz*76=456MHz
//    HPLL2CR |= 0x0d;    //HPLL2=6MHz*78=468MHz
//    HPLL2CR |= 0x0e;    //HPLL2=6MHz*80=480MHz
//    HPLL2CR |= 0x0f;    //HPLL2=6MHz*82=492MHz
    HPLL2CR |= 0x20;    //高速外设时钟选择 HPLL2/2
    HPLL2CR |= 0x80;    //使能HPLL2

    HPLLCR &= ~0x80;    //关闭HPLL（高速外设默认使用HPLL时钟，切换到HPLL2前要先使能HPLL，切换完再关闭HPLL）
    
    CLKDIV = 2;         //系统时钟 = 主时钟源/2 = HPLL2/2/2 = 90MHz
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
