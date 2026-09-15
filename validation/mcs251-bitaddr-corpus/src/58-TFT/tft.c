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
#undef WCOL
#define WCOL MCS251_SFRBIT(0xCD, 6)
#include "tft.h"

//LCD的画笔颜色和背景色       
u16 POINT_COLOR=0x0000; //画笔颜色
u16 BACK_COLOR=0xFFFF;  //背景色 

_lcd_dev lcddev;

//========================================================================
// 描述: SPI初始化函数
// 参数: 无
// 返回: 无
//========================================================================
void SPI_Init()
{
    P_LCD_CLK = 1;  //SCLK
    P_LCD_SDA = 1;  //MOSI
    P_LCD_RST = 1;  //RES
    P_LCD_DC  = 1;  //DC
    P_LCD_CS  = 1;  //CS

    P2M1 &= ~0x7c;  //SPI接口设置成推挽输出
    P2M0 |= 0x7c;
    
    P2SR &= ~0x28;  //P2.3(MOSI) P2.5(SCLK)设置成高速模式
 //   P2PU |= 0x54;   //SPI接口使能上拉电阻

    SPI_S1 = 0;     //00: P1.2 P1.3 P1.4 P1.5, 01: P2.2 P2.3 P2.4 P2.5, 10: P5.4 P4.0 P4.1 P4.2, 11: P3.5 P3.4 P3.3 P3.2
    SPI_S0 = 1;

    SSIG = 1;       //1: 忽略SS脚，由MSTR位决定主机还是从机        0: SS脚用于决定主机还是从机。
    SPEN = 1;       //1: 允许SPI，                                0: 禁止SPI，所有SPI管脚均为普通IO
    DORD = 0;       //1: LSB先发，                                0: MSB先发
    MSTR = 1;       //1: 设为主机                                 0: 设为从机
    CPOL = 1;       //1: 空闲时SCLK为高电平，                     0: 空闲时SCLK为低电平
    CPHA = 1;       //1: 数据在SCLK前沿驱动,后沿采样.              0: 数据在SCLK前沿采样,后沿驱动.
    SPCTL = (SPCTL & ~3) | 3;   //SPI 时钟频率选择, 0: 4T, 1: 8T,  2: 16T,  3: 2T

    SPI_CLKDIV = 2; //SPI_CLKDIV时钟分频

    SPIF = 1;       //清SPIF标志
    WCOL = 1;       //清WCOL标志

    HSSPI_CFG = 0;
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

/*****************************************************************************
 * @name       :void LCD_SendByte(u8 dat)
 * @date       :2018-11-13 
 * @function   :None
 * @parameters :None
 * @retvalue   :
******************************************************************************/ 
void LCD_SendByte(u8 dat)
{
    SPDAT = dat;            //发送一个字节
    while(SPIF == 0);       //等待发送完成
    SPSTAT = 0x80 + 0x40;   //清0 SPIF和WCOL标志
}

/*****************************************************************************
 * @name       :void LCD_WR_REG(u16 Reg)    
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters ::Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(u16 Reg)     
{
    P_LCD_DC = 0;//写命令
    P_LCD_CS = 0;
    LCD_SendByte((u8)Reg);
    P_LCD_CS = 1;
    P_LCD_DC = 1;//写数据
} 

/*****************************************************************************
 * @name       :void LCD_WR_DATA(u8 Data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit to the LCD screen
 * @parameters ::value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_DATA(u8 Data)
{
    P_LCD_CS = 0;
    LCD_SendByte(Data);
    P_LCD_CS = 1;
}

/*****************************************************************************
 * @name       :u16 LCD_RD_DATA(void)
 * @date       :2018-11-13 
 * @function   :Read an 16-bit value from the LCD screen
 * @parameters :None
 * @retvalue   :read value
******************************************************************************/
u16 LCD_RD_DATA(void)
{
    P_LCD_CS = 0;
    SPDAT = 0xff;
    while(SPIF == 0);       //等待发送完成
    SPSTAT = 0x80 + 0x40;   //清0 SPIF和WCOL标志
    P_LCD_CS = 1;
    return (SPDAT);
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA_16Bit(u16 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/     
void LCD_WR_DATA_16Bit(u16 Data)
{
    P_LCD_CS = 0;
    LCD_SendByte((u8)(Data>>8));
    LCD_SendByte((u8)Data);
    P_LCD_CS = 1;
}

u16 Color_To_565(u8 r, u8 g, u8 b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

/*****************************************************************************
 * @name       :u16 Lcd_ReadData_16Bit(void)
 * @date       :2018-11-13 
 * @function   :Read an 16-bit value from the LCD screen
 * @parameters :None
 * @retvalue   :read value
******************************************************************************/    
u16 Lcd_RD_DATA_16Bit(void)
{
    u16 r,g,b;

    //dummy 
    r = LCD_RD_DATA();
    //dummy 
    r = LCD_RD_DATA();
    //8bit:red 
    //16bit:red and green 
    r = LCD_RD_DATA();
    //8bit:green 
    //16bit:blue 
    g = LCD_RD_DATA();

    b = LCD_RD_DATA();

    return Color_To_565((u8)r, (u8)g, (u8)b);
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
{
    LCD_WR_REG(LCD_Reg);
    LCD_WR_DATA((u8)LCD_RegValue);
}

/*****************************************************************************
 * @name       :u16 LCD_ReadReg(u16 LCD_Reg)
 * @date       :2018-11-13 
 * @function   :read value from specially registers
 * @parameters :LCD_Reg:Register address
 * @retvalue   :read value
******************************************************************************/
void LCD_ReadReg(u16 LCD_Reg,u8 *Rval,int n)
{
    LCD_WR_REG((u8)LCD_Reg);
    while(n--)
    {
        *(Rval++) = LCD_RD_DATA();
    }
}

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/    
void LCD_WriteRAM_Prepare(void)
{
     LCD_WR_REG(lcddev.wramcmd);      
}

/*****************************************************************************
 * @name       :void LCD_ReadRAM_Prepare(void)
 * @date       :2018-11-13 
 * @function   :Read GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/     
void LCD_ReadRAM_Prepare(void)
{
    LCD_WR_REG(lcddev.rramcmd);
}

/*****************************************************************************
 * @name       :void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
                                yStar:the bebinning y coordinate of the LCD display window
                                xEnd:the endning x coordinate of the LCD display window
                                yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
{
    if(USE_HORIZONTAL == 2)
    {
        yStar += 80;
        yEnd += 80;
    }
    else if(USE_HORIZONTAL == 3)
    {
        xStar += 80;
        xEnd += 80;
    }

    LCD_WR_REG(lcddev.setxcmd);    
    LCD_WR_DATA((u8)(xStar>>8));
    LCD_WR_DATA(0x00FF&xStar);        
    LCD_WR_DATA((u8)(xEnd>>8));
    LCD_WR_DATA(0x00FF&xEnd);

    LCD_WR_REG(lcddev.setycmd);    
    LCD_WR_DATA((u8)(yStar>>8));
    LCD_WR_DATA(0x00FF&yStar);        
    LCD_WR_DATA((u8)(yEnd>>8));
    LCD_WR_DATA(0x00FF&yEnd);    

    LCD_WriteRAM_Prepare();    //开始写入GRAM
}

/*****************************************************************************
 * @name       :void LCD_Clear(u16 Color)
 * @date       :2018-08-09 
 * @function   :Full screen filled LCD screen
 * @parameters :color:Filled color
 * @retvalue   :None
******************************************************************************/    
void LCD_Clear(u16 Color)
{
    u16 i,j;
    LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);    
    for(i=0;i<lcddev.width;i++)
    {
        for (j=0;j<lcddev.height;j++)
        {
            LCD_WR_DATA_16Bit(Color);
        }
    }
}

/*****************************************************************************
 * @name       :void LCD_SetCursor(u16 Xpos, u16 Ypos)
 * @date       :2018-08-09 
 * @function   :Set coordinate value
 * @parameters :Xpos:the  x coordinate of the pixel
                                Ypos:the  y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{
    LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);    
}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(u16 x,u16 y)
 * @date       :2018-08-09 
 * @function   :Write a pixel at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/    
void LCD_DrawPoint(u16 x,u16 y)
{
    LCD_SetWindows(x,y,x,y);//设置光标位置 
    LCD_WR_DATA_16Bit(POINT_COLOR);         
}      

/*****************************************************************************
 * @name       :u16 LCD_ReadPoint(u16 x,u16 y)
 * @date       :2018-11-13 
 * @function   :Read a pixel color value at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :the read color value
******************************************************************************/    
u16 LCD_ReadPoint(u16 x,u16 y)
{
    u16 color;
    if(x>=lcddev.width||y>=lcddev.height)
    {
        return 0;	//超过了范围,直接返回	
    }
    LCD_SetCursor(x,y);//设置光标位置 
    LCD_ReadRAM_Prepare();
    color = Lcd_RD_DATA_16Bit();
    return color;
}

/*****************************************************************************
 * @name       :void LCD_direction(u8 direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
                          2-180 degree
                          3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(u8 direction)
{
    lcddev.setxcmd=0x2A;
    lcddev.setycmd=0x2B;
    lcddev.wramcmd=0x2C;
    lcddev.rramcmd=0x2E;
    switch(direction){
        case 0:
            lcddev.width=LCD_W;
            lcddev.height=LCD_H;
            LCD_WriteReg(0x36,0);
        break;
        case 1:
            lcddev.width=LCD_H;
            lcddev.height=LCD_W;
            LCD_WriteReg(0x36,(1<<5)|(1<<6));
        break;
        case 2:
            lcddev.width=LCD_W;
            lcddev.height=LCD_H;    
            LCD_WriteReg(0x36,(1<<4)|(1<<6)|(1<<7));
        break;
        case 3:
            lcddev.width=LCD_H;
            lcddev.height=LCD_W;
            LCD_WriteReg(0x36,(1<<4)|(1<<5)|(1<<7));
        break;
        default:break;
    }
}

/*****************************************************************************
 * @name       :void LCDReset(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/    
void LCDReset(void)
{
    delay_ms(50);    
    P_LCD_RST=0;
    delay_ms(50);
    P_LCD_RST=1;
    delay_ms(50);
}

/*****************************************************************************
 * @name       :void LCD_Init(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/          
void LCD_Init(void)
{
    LCDReset(); //初始化之前复位

    LCD_WR_REG(0x11);   //Sleep out 退出睡眠
    delay_ms(120);      //Delay 120ms

    LCD_WR_REG(0x3A);    //接口格式
    LCD_WR_DATA(0x05);

    LCD_WR_REG(0xB2);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x1F);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x33);
    LCD_WR_DATA(0x33);

    LCD_WR_REG(0xB7);
    LCD_WR_DATA(0x35);

    LCD_WR_REG(0xBB);
    LCD_WR_DATA(0x20);   //2b

    LCD_WR_REG(0xC0);
    LCD_WR_DATA(0x2C);

    LCD_WR_REG(0xC2);
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xC3);
    LCD_WR_DATA(0x01);

    LCD_WR_REG(0xC4);
    LCD_WR_DATA(0x18);   //VDV, 0x20:0v

    LCD_WR_REG(0xC6);
    LCD_WR_DATA(0x13);   //0x13:60Hz

    LCD_WR_REG(0xD0);
    LCD_WR_DATA(0xA4);
    LCD_WR_DATA(0xA1);

    LCD_WR_REG(0xD6);
    LCD_WR_DATA(0xA1);   //sleep in后，gate输出为GND

    //---------------ST7789V gamma setting-------------//
    LCD_WR_REG(0xE0);    //Set Gamma
    LCD_WR_DATA(0xF0);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x07);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x25);
    LCD_WR_DATA(0x33);
    LCD_WR_DATA(0x3C);
    LCD_WR_DATA(0x36);
    LCD_WR_DATA(0x14);
    LCD_WR_DATA(0x12);
    LCD_WR_DATA(0x29);
    LCD_WR_DATA(0x30);

    LCD_WR_REG(0xE1);    //Set Gamma
    LCD_WR_DATA(0xF0);
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x05);
    LCD_WR_DATA(0x05);
    LCD_WR_DATA(0x21);
    LCD_WR_DATA(0x25);
    LCD_WR_DATA(0x32);
    LCD_WR_DATA(0x3B);
    LCD_WR_DATA(0x38);
    LCD_WR_DATA(0x12);
    LCD_WR_DATA(0x14);
    LCD_WR_DATA(0x27);
    LCD_WR_DATA(0x31);

    LCD_WR_REG(0xE4);
    LCD_WR_DATA(0x1D);   //使用240根gate  (N+1)*8
    LCD_WR_DATA(0x00);   //设定gate起点位置
    LCD_WR_DATA(0x00);   //当gate没有用完时，bit4(TMG)设为0

    LCD_WR_REG(0x21);

    LCD_direction(USE_HORIZONTAL);

    LCD_WR_REG(0x29);    //开启显示
}

/******************************************************************************
      函数说明：在指定区域填充颜色
      入口数据：xsta,ysta   起始坐标
               xend,yend   终止坐标
               color       要填充的颜色
      返回值：  无
******************************************************************************/
void LCD_Fill(u16 xsta,u16 ysta,u16 xend,u16 yend,u16 color)
{
    u16 i,j;
    LCD_SetWindows(xsta,ysta,xend-1,yend-1);//设置显示范围
    for(i=ysta;i<yend;i++)
    {
        for(j=xsta;j<xend;j++)
        {
            LCD_WR_DATA_16Bit(color);
        }
    }
}

void TFT_ShowStart()
{
//    LCD_SetWindows(0,0,180-1,180-1);  //设置图片显示范围
    LCD_SetWindows(30,30,210-1,210-1);  //设置图片显示范围

	P_LCD_DC  = 1;	//写数据
	P_LCD_CS  = 0;	//片选
    DMA_SPI_CR |= 0x40;     //bit7 1:使能 SPI_DMA, bit6 1:开始 SPI_DMA 主机模式, bit0 1:清除 SPI_DMA FIFO
}

void TFT_ShowEnd()
{
    P_LCD_CS = 1;
    SPIF = 1;       //清SPIF标志
    WCOL = 1;       //清WCOL标志
}
