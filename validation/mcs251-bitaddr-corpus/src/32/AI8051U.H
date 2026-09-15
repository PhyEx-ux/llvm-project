#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef EAXFR
#define EAXFR MCS251_SFRBIT(0xBA, 7)
#undef N
#define N MCS251_SFRBIT(0xD1, 5)
/* official-header macros not provided by the compat headers (README 3.2) */
#define CHIPID ( (unsigned char volatile *)0x7efde0)
#define CHIPIDX ( (unsigned char volatile *)0x7efbd0)
#define USBBASE 0
#define FADDR (USBBASE + 0)
#define UPDATE 0x80
#define POWER (USBBASE + 1)
#define ISOUD 0x80
#define USBRSU 0x04
#define USBSUS 0x02
#define ENSUS 0x01
#define INTRIN1 (USBBASE + 2)
#define EP5INIF 0x20
#define EP4INIF 0x10
#define EP3INIF 0x08
#define EP2INIF 0x04
#define EP1INIF 0x02
#define EP0IF 0x01
#define INTROUT1 (USBBASE + 4)
#define EP5OUTIF 0x20
#define EP4OUTIF 0x10
#define EP3OUTIF 0x08
#define EP2OUTIF 0x04
#define EP1OUTIF 0x02
#define INTRUSB (USBBASE + 6)
#define SOFIF 0x08
#define RSTIF 0x04
#define RSUIF 0x02
#define SUSIF 0x01
#define INTRIN1E (USBBASE + 7)
#define EP5INIE 0x20
#define EP4INIE 0x10
#define EP3INIE 0x08
#define EP2INIE 0x04
#define EP1INIE 0x02
#define EP0IE 0x01
#define INTROUT1E (USBBASE + 9)
#define EP5OUTIE 0x20
#define EP4OUTIE 0x10
#define EP3OUTIE 0x08
#define EP2OUTIE 0x04
#define EP1OUTIE 0x02
#define INTRUSBE (USBBASE + 11)
#define SOFIE 0x08
#define RSTIE 0x04
#define RSUIE 0x02
#define SUSIE 0x01
#define FRAME1 (USBBASE + 12)
#define FRAME2 (USBBASE + 13)
#define INDEX (USBBASE + 14)
#define INMAXP (USBBASE + 16)
#define CSR0 (USBBASE + 17)
#define SSUEND 0x80
#define SOPRDY 0x40
#define SDSTL 0x20
#define SUEND 0x10
#define DATEND 0x08
#define STSTL 0x04
#define IPRDY 0x02
#define OPRDY 0x01
#define INCSR1 (USBBASE + 17)
#define INCLRDT 0x40
#define INSTSTL 0x20
#define INSDSTL 0x10
#define INFLUSH 0x08
#define INUNDRUN 0x04
#define INFIFONE 0x02
#define INIPRDY 0x01
#define INCSR2 (USBBASE + 18)
#define INAUTOSET 0x80
#define INISO 0x40
#define INMODEIN 0x20
#define INMODEOUT 0x00
#define INENDMA 0x10
#define INFCDT 0x08
#define OUTMAXP (USBBASE + 19)
#define OUTCSR1 (USBBASE + 20)
#define OUTCLRDT 0x80
#define OUTSTSTL 0x40
#define OUTSDSTL 0x20
#define OUTFLUSH 0x10
#define OUTDATERR 0x08
#define OUTOVRRUN 0x04
#define OUTFIFOFUL 0x02
#define OUTOPRDY 0x01
#define OUTCSR2 (USBBASE + 21)
#define OUTAUTOCLR 0x80
#define OUTISO 0x40
#define OUTENDMA 0x20
#define OUTDMAMD 0x10
#define COUNT0 (USBBASE + 22)
#define OUTCOUNT1 (USBBASE + 22)
#define OUTCOUNT2 (USBBASE + 23)
#define FIFO0 (USBBASE + 32)
#define FIFO1 (USBBASE + 33)
#define FIFO2 (USBBASE + 34)
#define FIFO3 (USBBASE + 35)
#define FIFO4 (USBBASE + 36)
#define FIFO5 (USBBASE + 37)
#define UTRKCTL (USBBASE + 48)
#define UTRKSTS (USBBASE + 49)
#define INT0_VECTOR 0       //0003H
#define TMR0_VECTOR 1       //000BH
#define INT1_VECTOR 2       //0013H
#define TMR1_VECTOR 3       //001BH
#define UART1_VECTOR 4       //0023H
#define ADC_VECTOR 5       //002BH
#define LVD_VECTOR 6       //0033H
#define PCA_VECTOR 7       //003BH
#define UART2_VECTOR 8       //0043H
#define SPI_VECTOR 9       //004BH
#define INT2_VECTOR 10      //0053H
#define INT3_VECTOR 11      //005BH
#define TMR2_VECTOR 12      //0063H
#define USER_VECTOR 13      //006BH
#define INT4_VECTOR 16      //0083H
#define UART3_VECTOR 17      //008BH
#define UART4_VECTOR 18      //0093H
#define TMR3_VECTOR 19      //009BH
#define TMR4_VECTOR 20      //00A3H
#define CMP_VECTOR 21      //00ABH
#define I2C_VECTOR 24      //00C3H
#define USB_VECTOR 25      //00CBH
#define PWMA_VECTOR 26      //00D3H
#define PWMB_VECTOR 27      //00DBH
#define RTC_VECTOR 36      //0123H
#define P0INT_VECTOR 37      //012BH
#define P1INT_VECTOR 38      //0133H
#define P2INT_VECTOR 39      //013BH
#define P3INT_VECTOR 40      //0143H
#define P4INT_VECTOR 41      //014BH
#define P5INT_VECTOR 42      //0153H
#define P6INT_VECTOR 43      //015BH
#define P7INT_VECTOR 44      //0163H
#define DMA_M2M_VECTOR 47      //017BH
#define DMA_ADC_VECTOR 48      //0183H
#define DMA_SPI_VECTOR 49      //018BH
#define DMA_UR1T_VECTOR 50      //0193H
#define DMA_UR1R_VECTOR 51      //019BH
#define DMA_UR2T_VECTOR 52      //01A3H
#define DMA_UR2R_VECTOR 53      //01ABH
#define DMA_UR3T_VECTOR 54      //01B3H
#define DMA_UR3R_VECTOR 55      //01BBH
#define DMA_UR4T_VECTOR 56      //01C3H
#define DMA_UR4R_VECTOR 57      //01CBH
#define DMA_LCM_VECTOR 58      //01D3H
#define LCM_VECTOR 59      //01DBH
#define DMA_I2CT_VECTOR 60      //01E3H
#define DMA_I2CR_VECTOR 61      //01EBH
#define I2S_VECTOR 62      //01F3H
#define DMA_I2ST_VECTOR 63      //01FBH
#define DMA_I2SR_VECTOR 64      //0203H
#define DMA_QSPI_VECTOR 65      //020BH
#define QSPI_VECTOR 66      //0213H
#define TMR11_VECTOR 67      //021BH
#define DMA_PWMAT_VECTOR 72      //0243H
#define DMA_PWMAR_VECTOR 73      //024BH
#define EAXSFR() EAXFR = 1		/* MOVX A,@DPTR/MOVX @DPTR,A鎸囦护鐨勬搷浣滃璞′负鎵╁睍SFR(XSFR) */
#define EAXRAM() EAXFR = 0		/* MOVX A,@DPTR/MOVX @DPTR,A鎸囦护鐨勬搷浣滃璞′负鎵╁睍RAM(XRAM) */
#define NOP1() _nop_()
#define NOP2() NOP1(),NOP1()
#define NOP3() NOP2(),NOP1()
#define NOP4() NOP3(),NOP1()
#define NOP5() NOP4(),NOP1()
#define NOP6() NOP5(),NOP1()
#define NOP7() NOP6(),NOP1()
#define NOP8() NOP7(),NOP1()
#define NOP9() NOP8(),NOP1()
#define NOP10() NOP9(),NOP1()
#define NOP11() NOP10(),NOP1()
#define NOP12() NOP11(),NOP1()
#define NOP13() NOP12(),NOP1()
#define NOP14() NOP13(),NOP1()
#define NOP15() NOP14(),NOP1()
#define NOP16() NOP15(),NOP1()
#define NOP17() NOP16(),NOP1()
#define NOP18() NOP17(),NOP1()
#define NOP19() NOP18(),NOP1()
#define NOP20() NOP19(),NOP1()
#define NOP21() NOP20(),NOP1()
#define NOP22() NOP21(),NOP1()
#define NOP23() NOP22(),NOP1()
#define NOP24() NOP23(),NOP1()
#define NOP25() NOP24(),NOP1()
#define NOP26() NOP25(),NOP1()
#define NOP27() NOP26(),NOP1()
#define NOP28() NOP27(),NOP1()
#define NOP29() NOP28(),NOP1()
#define NOP30() NOP29(),NOP1()
#define NOP31() NOP30(),NOP1()
#define NOP32() NOP31(),NOP1()
#define NOP33() NOP32(),NOP1()
#define NOP34() NOP33(),NOP1()
#define NOP35() NOP34(),NOP1()
#define NOP36() NOP35(),NOP1()
#define NOP37() NOP36(),NOP1()
#define NOP38() NOP37(),NOP1()
#define NOP39() NOP38(),NOP1()
#define NOP40() NOP39(),NOP1()
#define NOP(N) NOP##N()
/* sbit -> builtin unsigned char lvalue (README 3.1) */
#define P00 __builtin_mcs251_bit_lvalue(0x80)
#define P01 __builtin_mcs251_bit_lvalue(0x81)
#define P02 __builtin_mcs251_bit_lvalue(0x82)
#define P03 __builtin_mcs251_bit_lvalue(0x83)
#define P04 __builtin_mcs251_bit_lvalue(0x84)
#define P05 __builtin_mcs251_bit_lvalue(0x85)
#define P06 __builtin_mcs251_bit_lvalue(0x86)
#define P07 __builtin_mcs251_bit_lvalue(0x87)
#define P10 __builtin_mcs251_bit_lvalue(0x90)
#define P11 __builtin_mcs251_bit_lvalue(0x91)
#define P12 __builtin_mcs251_bit_lvalue(0x92)
#define P13 __builtin_mcs251_bit_lvalue(0x93)
#define P14 __builtin_mcs251_bit_lvalue(0x94)
#define P15 __builtin_mcs251_bit_lvalue(0x95)
#define P16 __builtin_mcs251_bit_lvalue(0x96)
#define P17 __builtin_mcs251_bit_lvalue(0x97)
#define P20 __builtin_mcs251_bit_lvalue(0xA0)
#define P21 __builtin_mcs251_bit_lvalue(0xA1)
#define P22 __builtin_mcs251_bit_lvalue(0xA2)
#define P23 __builtin_mcs251_bit_lvalue(0xA3)
#define P24 __builtin_mcs251_bit_lvalue(0xA4)
#define P25 __builtin_mcs251_bit_lvalue(0xA5)
#define P26 __builtin_mcs251_bit_lvalue(0xA6)
#define P27 __builtin_mcs251_bit_lvalue(0xA7)
#define P30 __builtin_mcs251_bit_lvalue(0xB0)
#define P31 __builtin_mcs251_bit_lvalue(0xB1)
#define P32 __builtin_mcs251_bit_lvalue(0xB2)
#define P33 __builtin_mcs251_bit_lvalue(0xB3)
#define P34 __builtin_mcs251_bit_lvalue(0xB4)
#define P35 __builtin_mcs251_bit_lvalue(0xB5)
#define P36 __builtin_mcs251_bit_lvalue(0xB6)
#define P37 __builtin_mcs251_bit_lvalue(0xB7)
#define RD __builtin_mcs251_bit_lvalue(0xB7)
#define WR __builtin_mcs251_bit_lvalue(0xB6)
#define T1 __builtin_mcs251_bit_lvalue(0xB5)
#define T0 __builtin_mcs251_bit_lvalue(0xB4)
#define INT1 __builtin_mcs251_bit_lvalue(0xB3)
#define INT0 __builtin_mcs251_bit_lvalue(0xB2)
#define TXD __builtin_mcs251_bit_lvalue(0xB1)
#define RXD __builtin_mcs251_bit_lvalue(0xB0)
#define P40 __builtin_mcs251_bit_lvalue(0xC0)
#define P41 __builtin_mcs251_bit_lvalue(0xC1)
#define P42 __builtin_mcs251_bit_lvalue(0xC2)
#define P43 __builtin_mcs251_bit_lvalue(0xC3)
#define P44 __builtin_mcs251_bit_lvalue(0xC4)
#define P45 __builtin_mcs251_bit_lvalue(0xC5)
#define P46 __builtin_mcs251_bit_lvalue(0xC6)
#define P47 __builtin_mcs251_bit_lvalue(0xC7)
#define P50 __builtin_mcs251_bit_lvalue(0xC8)
#define P51 __builtin_mcs251_bit_lvalue(0xC9)
#define P52 __builtin_mcs251_bit_lvalue(0xCA)
#define P53 __builtin_mcs251_bit_lvalue(0xCB)
#define P54 __builtin_mcs251_bit_lvalue(0xCC)
#define P55 __builtin_mcs251_bit_lvalue(0xCD)
#define P56 __builtin_mcs251_bit_lvalue(0xCE)
#define P57 __builtin_mcs251_bit_lvalue(0xCF)
#define P60 __builtin_mcs251_bit_lvalue(0xD0)
#define P61 __builtin_mcs251_bit_lvalue(0xD1)
#define P62 __builtin_mcs251_bit_lvalue(0xD2)
#define P63 __builtin_mcs251_bit_lvalue(0xD3)
#define P64 __builtin_mcs251_bit_lvalue(0xD4)
#define P65 __builtin_mcs251_bit_lvalue(0xD5)
#define P66 __builtin_mcs251_bit_lvalue(0xD6)
#define P67 __builtin_mcs251_bit_lvalue(0xD7)
#define P70 __builtin_mcs251_bit_lvalue(0xE0)
#define P71 __builtin_mcs251_bit_lvalue(0xE1)
#define P72 __builtin_mcs251_bit_lvalue(0xE2)
#define P73 __builtin_mcs251_bit_lvalue(0xE3)
#define P74 __builtin_mcs251_bit_lvalue(0xE4)
#define P75 __builtin_mcs251_bit_lvalue(0xE5)
#define P76 __builtin_mcs251_bit_lvalue(0xE6)
#define P77 __builtin_mcs251_bit_lvalue(0xE7)
#ifndef __AI8051U_H__
#define __AI8051U_H__

/////////////////////////////////////////////////


































































































































































































































































































































































































































































































/////////////////////////////////////////////////
//
//如下特殊功能寄存器位于扩展RAM区域
//访问这些寄存器,需先将EAXFR设置为1,才可正常读写
//    EAXFR = 1;
//或者
//    P_SW2 |= 0x80;
///////////////////////////////////////////////////

/////////////////////////////////////////////////
//7E:FF00H-7E:FFFFH
/////////////////////////////////////////////////



/////////////////////////////////////////////////
//7E:FE00H-7E:FEFFH
/////////////////////////////////////////////////

#define     CLKSEL                  (*(unsigned char volatile __xdata *)0x7efe00)
#define     CLKDIV                  (*(unsigned char volatile __xdata *)0x7efe01)
#define     HIRCCR                  (*(unsigned char volatile __xdata *)0x7efe02)
#define     XOSCCR                  (*(unsigned char volatile __xdata *)0x7efe03)
#define     IRC32KCR                (*(unsigned char volatile __xdata *)0x7efe04)
#define     MCLKOCR                 (*(unsigned char volatile __xdata *)0x7efe05)
#define     IRCDB                   (*(unsigned char volatile __xdata *)0x7efe06)
#define     IRC48MCR                (*(unsigned char volatile __xdata *)0x7efe07)
#define     X32KCR                  (*(unsigned char volatile __xdata *)0x7efe08)
#define     HSCLKDIV                (*(unsigned char volatile __xdata *)0x7efe0b)

#define     P0PU                    (*(unsigned char volatile __xdata *)0x7efe10)
#define     P1PU                    (*(unsigned char volatile __xdata *)0x7efe11)
#define     P2PU                    (*(unsigned char volatile __xdata *)0x7efe12)
#define     P3PU                    (*(unsigned char volatile __xdata *)0x7efe13)
#define     P4PU                    (*(unsigned char volatile __xdata *)0x7efe14)
#define     P5PU                    (*(unsigned char volatile __xdata *)0x7efe15)
#define     P6PU                    (*(unsigned char volatile __xdata *)0x7efe16)
#define     P7PU                    (*(unsigned char volatile __xdata *)0x7efe17)
#define     P0NCS                   (*(unsigned char volatile __xdata *)0x7efe18)
#define     P1NCS                   (*(unsigned char volatile __xdata *)0x7efe19)
#define     P2NCS                   (*(unsigned char volatile __xdata *)0x7efe1a)
#define     P3NCS                   (*(unsigned char volatile __xdata *)0x7efe1b)
#define     P4NCS                   (*(unsigned char volatile __xdata *)0x7efe1c)
#define     P5NCS                   (*(unsigned char volatile __xdata *)0x7efe1d)
#define     P6NCS                   (*(unsigned char volatile __xdata *)0x7efe1e)
#define     P7NCS                   (*(unsigned char volatile __xdata *)0x7efe1f)
#define     P0SR                    (*(unsigned char volatile __xdata *)0x7efe20)
#define     P1SR                    (*(unsigned char volatile __xdata *)0x7efe21)
#define     P2SR                    (*(unsigned char volatile __xdata *)0x7efe22)
#define     P3SR                    (*(unsigned char volatile __xdata *)0x7efe23)
#define     P4SR                    (*(unsigned char volatile __xdata *)0x7efe24)
#define     P5SR                    (*(unsigned char volatile __xdata *)0x7efe25)
#define     P6SR                    (*(unsigned char volatile __xdata *)0x7efe26)
#define     P7SR                    (*(unsigned char volatile __xdata *)0x7efe27)
#define     P0DR                    (*(unsigned char volatile __xdata *)0x7efe28)
#define     P1DR                    (*(unsigned char volatile __xdata *)0x7efe29)
#define     P2DR                    (*(unsigned char volatile __xdata *)0x7efe2a)
#define     P3DR                    (*(unsigned char volatile __xdata *)0x7efe2b)
#define     P4DR                    (*(unsigned char volatile __xdata *)0x7efe2c)
#define     P5DR                    (*(unsigned char volatile __xdata *)0x7efe2d)
#define     P6DR                    (*(unsigned char volatile __xdata *)0x7efe2e)
#define     P7DR                    (*(unsigned char volatile __xdata *)0x7efe2f)
#define     P0IE                    (*(unsigned char volatile __xdata *)0x7efe30)
#define     P1IE                    (*(unsigned char volatile __xdata *)0x7efe31)
#define     P2IE                    (*(unsigned char volatile __xdata *)0x7efe32)
#define     P3IE                    (*(unsigned char volatile __xdata *)0x7efe33)
#define     P4IE                    (*(unsigned char volatile __xdata *)0x7efe34)
#define     P5IE                    (*(unsigned char volatile __xdata *)0x7efe35)
#define     P6IE                    (*(unsigned char volatile __xdata *)0x7efe36)
#define     P7IE                    (*(unsigned char volatile __xdata *)0x7efe37)
#define     P0PD                    (*(unsigned char volatile __xdata *)0x7efe40)
#define     P1PD                    (*(unsigned char volatile __xdata *)0x7efe41)
#define     P2PD                    (*(unsigned char volatile __xdata *)0x7efe42)
#define     P3PD                    (*(unsigned char volatile __xdata *)0x7efe43)
#define     P4PD                    (*(unsigned char volatile __xdata *)0x7efe44)
#define     P5PD                    (*(unsigned char volatile __xdata *)0x7efe45)
#define     P6PD                    (*(unsigned char volatile __xdata *)0x7efe46)
#define     P7PD                    (*(unsigned char volatile __xdata *)0x7efe47)
#define     P0BP                    (*(unsigned char volatile __xdata *)0x7efe48)
#define     P1BP                    (*(unsigned char volatile __xdata *)0x7efe49)
#define     P2BP                    (*(unsigned char volatile __xdata *)0x7efe4a)
#define     P3BP                    (*(unsigned char volatile __xdata *)0x7efe4b)
#define     P4BP                    (*(unsigned char volatile __xdata *)0x7efe4c)
#define     P5BP                    (*(unsigned char volatile __xdata *)0x7efe4d)
#define     P6BP                    (*(unsigned char volatile __xdata *)0x7efe4e)
#define     P7BP                    (*(unsigned char volatile __xdata *)0x7efe4f)

#define     LCMIFCFG                (*(unsigned char volatile __xdata *)0x7efe50)
#define     LCMIFCFG2               (*(unsigned char volatile __xdata *)0x7efe51)
#define     LCMIFCR                 (*(unsigned char volatile __xdata *)0x7efe52)
#define     LCMIFSTA                (*(unsigned char volatile __xdata *)0x7efe53)
#define     LCMIFDATL               (*(unsigned char volatile __xdata *)0x7efe54)
#define     LCMIFDATH               (*(unsigned char volatile __xdata *)0x7efe55)
#define     LCMIFPSCR               (*(unsigned char volatile __xdata *)0x7efe56)

#define     RTCCR                   (*(unsigned char volatile __xdata *)0x7efe60)
#define     RTCCFG                  (*(unsigned char volatile __xdata *)0x7efe61)
#define     RTCIEN                  (*(unsigned char volatile __xdata *)0x7efe62)
#define     RTCIF                   (*(unsigned char volatile __xdata *)0x7efe63)
#define     ALAHOUR                 (*(unsigned char volatile __xdata *)0x7efe64)
#define     ALAMIN                  (*(unsigned char volatile __xdata *)0x7efe65)
#define     ALASEC                  (*(unsigned char volatile __xdata *)0x7efe66)
#define     ALASSEC                 (*(unsigned char volatile __xdata *)0x7efe67)
#define     INIYEAR                 (*(unsigned char volatile __xdata *)0x7efe68)
#define     INIMONTH                (*(unsigned char volatile __xdata *)0x7efe69)
#define     INIDAY                  (*(unsigned char volatile __xdata *)0x7efe6a)
#define     INIHOUR                 (*(unsigned char volatile __xdata *)0x7efe6b)
#define     INIMIN                  (*(unsigned char volatile __xdata *)0x7efe6c)
#define     INISEC                  (*(unsigned char volatile __xdata *)0x7efe6d)
#define     INISSEC                 (*(unsigned char volatile __xdata *)0x7efe6e)
#define     INIWEEK                 (*(unsigned char volatile __xdata *)0x7efe6f)
#define     WEEK                    (*(unsigned char volatile __xdata *)0x7efe6f)
#define     YEAR                    (*(unsigned char volatile __xdata *)0x7efe70)
#define     MONTH                   (*(unsigned char volatile __xdata *)0x7efe71)
#define     DAY                     (*(unsigned char volatile __xdata *)0x7efe72)
#define     HOUR                    (*(unsigned char volatile __xdata *)0x7efe73)
#define     MIN                     (*(unsigned char volatile __xdata *)0x7efe74)
#define     SEC                     (*(unsigned char volatile __xdata *)0x7efe75)
#define     SSEC                    (*(unsigned char volatile __xdata *)0x7efe76)

#define     T11CR                   (*(unsigned char volatile __xdata *)0x7efe78)
#define     T11PS                   (*(unsigned char volatile __xdata *)0x7efe79)
#define     T11H                    (*(unsigned char volatile __xdata *)0x7efe7a)
#define     T11L                    (*(unsigned char volatile __xdata *)0x7efe7b)

#define     I2CCFG                  (*(unsigned char volatile __xdata *)0x7efe80)
#define     I2CMSCR                 (*(unsigned char volatile __xdata *)0x7efe81)
#define     I2CMSST                 (*(unsigned char volatile __xdata *)0x7efe82)
#define     I2CSLCR                 (*(unsigned char volatile __xdata *)0x7efe83)
#define     I2CSLST                 (*(unsigned char volatile __xdata *)0x7efe84)
#define     I2CSLADR                (*(unsigned char volatile __xdata *)0x7efe85)
#define     I2CTXD                  (*(unsigned char volatile __xdata *)0x7efe86)
#define     I2CRXD                  (*(unsigned char volatile __xdata *)0x7efe87)
#define     I2CMSAUX                (*(unsigned char volatile __xdata *)0x7efe88)
#define     I2CPSCR                 (*(unsigned char volatile __xdata *)0x7efe89)

#define     SPI_CLKDIV              (*(unsigned char volatile __xdata *)0x7efe90)
#define     PWMA_CLKDIV             (*(unsigned char volatile __xdata *)0x7efe91)
#define     PWMB_CLKDIV             (*(unsigned char volatile __xdata *)0x7efe92)
#define     TFPU_CLKDIV             (*(unsigned char volatile __xdata *)0x7efe93)
#define     I2S_CLKDIV              (*(unsigned char volatile __xdata *)0x7efe94)

#define     RSTFLAG                 (*(unsigned char volatile __xdata *)0x7efe99)
#define     RSTCR0                  (*(unsigned char volatile __xdata *)0x7efe9a)
#define     RSTCR1                  (*(unsigned char volatile __xdata *)0x7efe9b)
#define     RSTCR2                  (*(unsigned char volatile __xdata *)0x7efe9c)
#define     RSTCR3                  (*(unsigned char volatile __xdata *)0x7efe9d)
#define     RSTCR4                  (*(unsigned char volatile __xdata *)0x7efe9e)
#define     RSTCR5                  (*(unsigned char volatile __xdata *)0x7efe9f)

#define     TM0PS                   (*(unsigned char volatile __xdata *)0x7efea0)
#define     TM1PS                   (*(unsigned char volatile __xdata *)0x7efea1)
#define     TM2PS                   (*(unsigned char volatile __xdata *)0x7efea2)
#define     TM3PS                   (*(unsigned char volatile __xdata *)0x7efea3)
#define     TM4PS                   (*(unsigned char volatile __xdata *)0x7efea4)
#define     ADCTIM                  (*(unsigned char volatile __xdata *)0x7efea8)
#define     ADCEXCFG                (*(unsigned char volatile __xdata *)0x7efead)
#define     CMPEXCFG                (*(unsigned char volatile __xdata *)0x7efeae)

#define     PWMA_ETRPS              (*(unsigned char volatile __xdata *)0x7efeb0)
#define     PWMA_ENO                (*(unsigned char volatile __xdata *)0x7efeb1)
#define     PWMA_PS                 (*(unsigned char volatile __xdata *)0x7efeb2)
#define     PWMA_IOAUX              (*(unsigned char volatile __xdata *)0x7efeb3)
#define     PWMB_ETRPS              (*(unsigned char volatile __xdata *)0x7efeb4)
#define     PWMB_ENO                (*(unsigned char volatile __xdata *)0x7efeb5)
#define     PWMB_PS                 (*(unsigned char volatile __xdata *)0x7efeb6)
#define     PWMB_IOAUX              (*(unsigned char volatile __xdata *)0x7efeb7)
#define     PWMA_PS2                (*(unsigned char volatile __xdata *)0x7efeb8)
#define     PWMA_RCRH               (*(unsigned char volatile __xdata *)0x7efeb9)
#define     PWMB_RCRH               (*(unsigned char volatile __xdata *)0x7efeba)

#define     PWMA_CR1                (*(unsigned char volatile __xdata *)0x7efec0)
#define     PWMA_CR2                (*(unsigned char volatile __xdata *)0x7efec1)
#define     PWMA_SMCR               (*(unsigned char volatile __xdata *)0x7efec2)
#define     PWMA_ETR                (*(unsigned char volatile __xdata *)0x7efec3)
#define     PWMA_IER                (*(unsigned char volatile __xdata *)0x7efec4)
#define     PWMA_SR1                (*(unsigned char volatile __xdata *)0x7efec5)
#define     PWMA_SR2                (*(unsigned char volatile __xdata *)0x7efec6)
#define     PWMA_EGR                (*(unsigned char volatile __xdata *)0x7efec7)
#define     PWMA_CCMR1              (*(unsigned char volatile __xdata *)0x7efec8)
#define     PWMA_CCMR2              (*(unsigned char volatile __xdata *)0x7efec9)
#define     PWMA_CCMR3              (*(unsigned char volatile __xdata *)0x7efeca)
#define     PWMA_CCMR4              (*(unsigned char volatile __xdata *)0x7efecb)
#define     PWMA_CCER1              (*(unsigned char volatile __xdata *)0x7efecc)
#define     PWMA_CCER2              (*(unsigned char volatile __xdata *)0x7efecd)
#define     PWMA_CNTRH              (*(unsigned char volatile __xdata *)0x7efece)
#define     PWMA_CNTRL              (*(unsigned char volatile __xdata *)0x7efecf)
#define     PWMA_PSCRH              (*(unsigned char volatile __xdata *)0x7efed0)
#define     PWMA_PSCRL              (*(unsigned char volatile __xdata *)0x7efed1)
#define     PWMA_ARRH               (*(unsigned char volatile __xdata *)0x7efed2)
#define     PWMA_ARRL               (*(unsigned char volatile __xdata *)0x7efed3)
#define     PWMA_RCR                (*(unsigned char volatile __xdata *)0x7efed4)
#define     PWMA_CCR1H              (*(unsigned char volatile __xdata *)0x7efed5)
#define     PWMA_CCR1L              (*(unsigned char volatile __xdata *)0x7efed6)
#define     PWMA_CCR2H              (*(unsigned char volatile __xdata *)0x7efed7)
#define     PWMA_CCR2L              (*(unsigned char volatile __xdata *)0x7efed8)
#define     PWMA_CCR3H              (*(unsigned char volatile __xdata *)0x7efed9)
#define     PWMA_CCR3L              (*(unsigned char volatile __xdata *)0x7efeda)
#define     PWMA_CCR4H              (*(unsigned char volatile __xdata *)0x7efedb)
#define     PWMA_CCR4L              (*(unsigned char volatile __xdata *)0x7efedc)
#define     PWMA_BKR                (*(unsigned char volatile __xdata *)0x7efedd)
#define     PWMA_DTR                (*(unsigned char volatile __xdata *)0x7efede)
#define     PWMA_OISR               (*(unsigned char volatile __xdata *)0x7efedf)

#define     PWMB_CR1                (*(unsigned char volatile __xdata *)0x7efee0)
#define     PWMB_CR2                (*(unsigned char volatile __xdata *)0x7efee1)
#define     PWMB_SMCR               (*(unsigned char volatile __xdata *)0x7efee2)
#define     PWMB_ETR                (*(unsigned char volatile __xdata *)0x7efee3)
#define     PWMB_IER                (*(unsigned char volatile __xdata *)0x7efee4)
#define     PWMB_SR1                (*(unsigned char volatile __xdata *)0x7efee5)
#define     PWMB_SR2                (*(unsigned char volatile __xdata *)0x7efee6)
#define     PWMB_EGR                (*(unsigned char volatile __xdata *)0x7efee7)
#define     PWMB_CCMR1              (*(unsigned char volatile __xdata *)0x7efee8)
#define     PWMB_CCMR2              (*(unsigned char volatile __xdata *)0x7efee9)
#define     PWMB_CCMR3              (*(unsigned char volatile __xdata *)0x7efeea)
#define     PWMB_CCMR4              (*(unsigned char volatile __xdata *)0x7efeeb)
#define     PWMB_CCER1              (*(unsigned char volatile __xdata *)0x7efeec)
#define     PWMB_CCER2              (*(unsigned char volatile __xdata *)0x7efeed)
#define     PWMB_CNTRH              (*(unsigned char volatile __xdata *)0x7efeee)
#define     PWMB_CNTRL              (*(unsigned char volatile __xdata *)0x7efeef)
#define     PWMB_PSCRH              (*(unsigned char volatile __xdata *)0x7efef0)
#define     PWMB_PSCRL              (*(unsigned char volatile __xdata *)0x7efef1)
#define     PWMB_ARRH               (*(unsigned char volatile __xdata *)0x7efef2)
#define     PWMB_ARRL               (*(unsigned char volatile __xdata *)0x7efef3)
#define     PWMB_RCR                (*(unsigned char volatile __xdata *)0x7efef4)
#define     PWMB_CCR5H              (*(unsigned char volatile __xdata *)0x7efef5)
#define     PWMB_CCR5L              (*(unsigned char volatile __xdata *)0x7efef6)
#define     PWMB_CCR6H              (*(unsigned char volatile __xdata *)0x7efef7)
#define     PWMB_CCR6L              (*(unsigned char volatile __xdata *)0x7efef8)
#define     PWMB_CCR7H              (*(unsigned char volatile __xdata *)0x7efef9)
#define     PWMB_CCR7L              (*(unsigned char volatile __xdata *)0x7efefa)
#define     PWMB_CCR8H              (*(unsigned char volatile __xdata *)0x7efefb)
#define     PWMB_CCR8L              (*(unsigned char volatile __xdata *)0x7efefc)
#define     PWMB_BKR                (*(unsigned char volatile __xdata *)0x7efefd)
#define     PWMB_DTR                (*(unsigned char volatile __xdata *)0x7efefe)
#define     PWMB_OISR               (*(unsigned char volatile __xdata *)0x7efeff)

/////////////////////////////////////////////////
//7E:FD00H-7E:FDFFH
/////////////////////////////////////////////////

#define     P0INTE                  (*(unsigned char volatile __xdata *)0x7efd00)
#define     P1INTE                  (*(unsigned char volatile __xdata *)0x7efd01)
#define     P2INTE                  (*(unsigned char volatile __xdata *)0x7efd02)
#define     P3INTE                  (*(unsigned char volatile __xdata *)0x7efd03)
#define     P4INTE                  (*(unsigned char volatile __xdata *)0x7efd04)
#define     P5INTE                  (*(unsigned char volatile __xdata *)0x7efd05)
#define     P6INTE                  (*(unsigned char volatile __xdata *)0x7efd06)
#define     P7INTE                  (*(unsigned char volatile __xdata *)0x7efd07)
#define     P0INTF                  (*(unsigned char volatile __xdata *)0x7efd10)
#define     P1INTF                  (*(unsigned char volatile __xdata *)0x7efd11)
#define     P2INTF                  (*(unsigned char volatile __xdata *)0x7efd12)
#define     P3INTF                  (*(unsigned char volatile __xdata *)0x7efd13)
#define     P4INTF                  (*(unsigned char volatile __xdata *)0x7efd14)
#define     P5INTF                  (*(unsigned char volatile __xdata *)0x7efd15)
#define     P6INTF                  (*(unsigned char volatile __xdata *)0x7efd16)
#define     P7INTF                  (*(unsigned char volatile __xdata *)0x7efd17)
#define     P0IM0                   (*(unsigned char volatile __xdata *)0x7efd20)
#define     P1IM0                   (*(unsigned char volatile __xdata *)0x7efd21)
#define     P2IM0                   (*(unsigned char volatile __xdata *)0x7efd22)
#define     P3IM0                   (*(unsigned char volatile __xdata *)0x7efd23)
#define     P4IM0                   (*(unsigned char volatile __xdata *)0x7efd24)
#define     P5IM0                   (*(unsigned char volatile __xdata *)0x7efd25)
#define     P6IM0                   (*(unsigned char volatile __xdata *)0x7efd26)
#define     P7IM0                   (*(unsigned char volatile __xdata *)0x7efd27)
#define     P0IM1                   (*(unsigned char volatile __xdata *)0x7efd30)
#define     P1IM1                   (*(unsigned char volatile __xdata *)0x7efd31)
#define     P2IM1                   (*(unsigned char volatile __xdata *)0x7efd32)
#define     P3IM1                   (*(unsigned char volatile __xdata *)0x7efd33)
#define     P4IM1                   (*(unsigned char volatile __xdata *)0x7efd34)
#define     P5IM1                   (*(unsigned char volatile __xdata *)0x7efd35)
#define     P6IM1                   (*(unsigned char volatile __xdata *)0x7efd36)
#define     P7IM1                   (*(unsigned char volatile __xdata *)0x7efd37)
#define     P0WKUE                  (*(unsigned char volatile __xdata *)0x7efd40)
#define     P1WKUE                  (*(unsigned char volatile __xdata *)0x7efd41)
#define     P2WKUE                  (*(unsigned char volatile __xdata *)0x7efd42)
#define     P3WKUE                  (*(unsigned char volatile __xdata *)0x7efd43)
#define     P4WKUE                  (*(unsigned char volatile __xdata *)0x7efd44)
#define     P5WKUE                  (*(unsigned char volatile __xdata *)0x7efd45)
#define     P6WKUE                  (*(unsigned char volatile __xdata *)0x7efd46)
#define     P7WKUE                  (*(unsigned char volatile __xdata *)0x7efd47)

#define     CCAPM2                  (*(unsigned char volatile __xdata *)0x7efd50)
#define     CCAP2L                  (*(unsigned char volatile __xdata *)0x7efd51)
#define     CCAP2H                  (*(unsigned char volatile __xdata *)0x7efd52)
#define     PCA_PWM2                (*(unsigned char volatile __xdata *)0x7efd53)
#define     CCAPM3                  (*(unsigned char volatile __xdata *)0x7efd54)
#define     CCAP3L                  (*(unsigned char volatile __xdata *)0x7efd55)
#define     CCAP3H                  (*(unsigned char volatile __xdata *)0x7efd56)
#define     PCA_PWM3                (*(unsigned char volatile __xdata *)0x7efd57)
#define     CCAPM0                  (*(unsigned char volatile __xdata *)0x7efd58)
#define     CCAP0L                  (*(unsigned char volatile __xdata *)0x7efd59)
#define     CCAP0H                  (*(unsigned char volatile __xdata *)0x7efd5a)
#define     PCA_PWM0                (*(unsigned char volatile __xdata *)0x7efd5b)
#define     CCAPM1                  (*(unsigned char volatile __xdata *)0x7efd5c)
#define     CCAP1L                  (*(unsigned char volatile __xdata *)0x7efd5d)
#define     CCAP1H                  (*(unsigned char volatile __xdata *)0x7efd5e)
#define     PCA_PWM1                (*(unsigned char volatile __xdata *)0x7efd5f)

#define     PINIPL                  (*(unsigned char volatile __xdata *)0x7efd60)
#define     PINIPH                  (*(unsigned char volatile __xdata *)0x7efd61)

#define     CCON                    (*(unsigned char volatile __xdata *)0x7efd64)
#define     CL                      (*(unsigned char volatile __xdata *)0x7efd65)
#define     CH                      (*(unsigned char volatile __xdata *)0x7efd66)
#define     CMOD                    (*(unsigned char volatile __xdata *)0x7efd67)

#define     UR1TOCR                 (*(unsigned char volatile __xdata *)0x7efd70)
#define     UR1TOSR                 (*(unsigned char volatile __xdata *)0x7efd71)
#define     UR1TOTH                 (*(unsigned char volatile __xdata *)0x7efd72)
#define     UR1TOTL                 (*(unsigned char volatile __xdata *)0x7efd73)
#define     UR2TOCR                 (*(unsigned char volatile __xdata *)0x7efd74)
#define     UR2TOSR                 (*(unsigned char volatile __xdata *)0x7efd75)
#define     UR2TOTH                 (*(unsigned char volatile __xdata *)0x7efd76)
#define     UR2TOTL                 (*(unsigned char volatile __xdata *)0x7efd77)
#define     UR3TOCR                 (*(unsigned char volatile __xdata *)0x7efd78)
#define     UR3TOSR                 (*(unsigned char volatile __xdata *)0x7efd79)
#define     UR3TOTH                 (*(unsigned char volatile __xdata *)0x7efd7a)
#define     UR3TOTL                 (*(unsigned char volatile __xdata *)0x7efd7b)
#define     UR4TOCR                 (*(unsigned char volatile __xdata *)0x7efd7c)
#define     UR4TOSR                 (*(unsigned char volatile __xdata *)0x7efd7d)
#define     UR4TOTH                 (*(unsigned char volatile __xdata *)0x7efd7e)
#define     UR4TOTL                 (*(unsigned char volatile __xdata *)0x7efd7f)
#define     SPITOCR                 (*(unsigned char volatile __xdata *)0x7efd80)
#define     SPITOSR                 (*(unsigned char volatile __xdata *)0x7efd81)
#define     SPITOTH                 (*(unsigned char volatile __xdata *)0x7efd82)
#define     SPITOTL                 (*(unsigned char volatile __xdata *)0x7efd83)
#define     I2CTOCR                 (*(unsigned char volatile __xdata *)0x7efd84)
#define     I2CTOSR                 (*(unsigned char volatile __xdata *)0x7efd85)
#define     I2CTOTH                 (*(unsigned char volatile __xdata *)0x7efd86)
#define     I2CTOTL                 (*(unsigned char volatile __xdata *)0x7efd87)

#define     UR1TOTE                 (*(unsigned char volatile __xdata *)0x7efd88)
#define     UR2TOTE                 (*(unsigned char volatile __xdata *)0x7efd89)
#define     UR3TOTE                 (*(unsigned char volatile __xdata *)0x7efd8a)
#define     UR4TOTE                 (*(unsigned char volatile __xdata *)0x7efd8b)
#define     SPITOTE                 (*(unsigned char volatile __xdata *)0x7efd8c)
#define     I2CTOTE                 (*(unsigned char volatile __xdata *)0x7efd8d)

#define     I2SCR                   (*(unsigned char volatile __xdata *)0x7efd98)
#define     I2SSR                   (*(unsigned char volatile __xdata *)0x7efd99)
#define     I2SDRH                  (*(unsigned char volatile __xdata *)0x7efd9a)
#define     I2SDRL                  (*(unsigned char volatile __xdata *)0x7efd9b)
#define     I2SPRH                  (*(unsigned char volatile __xdata *)0x7efd9c)
#define     I2SPRL                  (*(unsigned char volatile __xdata *)0x7efd9d)
#define     I2SCFGH                 (*(unsigned char volatile __xdata *)0x7efd9e)
#define     I2SCFGL                 (*(unsigned char volatile __xdata *)0x7efd9f)
#define     I2SMD                   (*(unsigned char volatile __xdata *)0x7efda0)
#define     I2SMCKDIV               (*(unsigned char volatile __xdata *)0x7efda1)

#define     CRECR                   (*(unsigned char volatile __xdata *)0x7efda8)
#define     CRECNTH                 (*(unsigned char volatile __xdata *)0x7efda9)
#define     CRECNTL                 (*(unsigned char volatile __xdata *)0x7efdaa)
#define     CRERES                  (*(unsigned char volatile __xdata *)0x7efdab)

#define     S2CFG                   (*(unsigned char volatile __xdata *)0x7efdb4)
#define     S2ADDR                  (*(unsigned char volatile __xdata *)0x7efdb5)
#define     S2ADEN                  (*(unsigned char volatile __xdata *)0x7efdb6)
#define     USARTCR1                (*(unsigned char volatile __xdata *)0x7efdc0)
#define     USARTCR2                (*(unsigned char volatile __xdata *)0x7efdc1)
#define     USARTCR3                (*(unsigned char volatile __xdata *)0x7efdc2)
#define     USARTCR4                (*(unsigned char volatile __xdata *)0x7efdc3)
#define     USARTCR5                (*(unsigned char volatile __xdata *)0x7efdc4)
#define     USARTGTR                (*(unsigned char volatile __xdata *)0x7efdc5)
#define     USARTBRH                (*(unsigned char volatile __xdata *)0x7efdc6)
#define     USARTBRL                (*(unsigned char volatile __xdata *)0x7efdc7)
#define     USART2CR1               (*(unsigned char volatile __xdata *)0x7efdc8)
#define     USART2CR2               (*(unsigned char volatile __xdata *)0x7efdc9)
#define     USART2CR3               (*(unsigned char volatile __xdata *)0x7efdca)
#define     USART2CR4               (*(unsigned char volatile __xdata *)0x7efdcb)
#define     USART2CR5               (*(unsigned char volatile __xdata *)0x7efdcc)
#define     USART2GTR               (*(unsigned char volatile __xdata *)0x7efdcd)
#define     USART2BRH               (*(unsigned char volatile __xdata *)0x7efdce)
#define     USART2BRL               (*(unsigned char volatile __xdata *)0x7efdcf)

#define     CHIPID                  ( (unsigned char volatile __xdata *)0x7efde0)

#define     CHIPID0                 (*(unsigned char volatile __xdata *)0x7efde0)
#define     CHIPID1                 (*(unsigned char volatile __xdata *)0x7efde1)
#define     CHIPID2                 (*(unsigned char volatile __xdata *)0x7efde2)
#define     CHIPID3                 (*(unsigned char volatile __xdata *)0x7efde3)
#define     CHIPID4                 (*(unsigned char volatile __xdata *)0x7efde4)
#define     CHIPID5                 (*(unsigned char volatile __xdata *)0x7efde5)
#define     CHIPID6                 (*(unsigned char volatile __xdata *)0x7efde6)
#define     CHIPID7                 (*(unsigned char volatile __xdata *)0x7efde7)
#define     CHIPID8                 (*(unsigned char volatile __xdata *)0x7efde8)
#define     CHIPID9                 (*(unsigned char volatile __xdata *)0x7efde9)
#define     CHIPID10                (*(unsigned char volatile __xdata *)0x7efdea)
#define     CHIPID11                (*(unsigned char volatile __xdata *)0x7efdeb)
#define     CHIPID12                (*(unsigned char volatile __xdata *)0x7efdec)
#define     CHIPID13                (*(unsigned char volatile __xdata *)0x7efded)
#define     CHIPID14                (*(unsigned char volatile __xdata *)0x7efdee)
#define     CHIPID15                (*(unsigned char volatile __xdata *)0x7efdef)
#define     CHIPID16                (*(unsigned char volatile __xdata *)0x7efdf0)
#define     CHIPID17                (*(unsigned char volatile __xdata *)0x7efdf1)
#define     CHIPID18                (*(unsigned char volatile __xdata *)0x7efdf2)
#define     CHIPID19                (*(unsigned char volatile __xdata *)0x7efdf3)
#define     CHIPID20                (*(unsigned char volatile __xdata *)0x7efdf4)
#define     CHIPID21                (*(unsigned char volatile __xdata *)0x7efdf5)
#define     CHIPID22                (*(unsigned char volatile __xdata *)0x7efdf6)
#define     CHIPID23                (*(unsigned char volatile __xdata *)0x7efdf7)
#define     CHIPID24                (*(unsigned char volatile __xdata *)0x7efdf8)
#define     CHIPID25                (*(unsigned char volatile __xdata *)0x7efdf9)
#define     CHIPID26                (*(unsigned char volatile __xdata *)0x7efdfa)
#define     CHIPID27                (*(unsigned char volatile __xdata *)0x7efdfb)
#define     CHIPID28                (*(unsigned char volatile __xdata *)0x7efdfc)
#define     CHIPID29                (*(unsigned char volatile __xdata *)0x7efdfd)
#define     CHIPID30                (*(unsigned char volatile __xdata *)0x7efdfe)
#define     CHIPID31                (*(unsigned char volatile __xdata *)0x7efdff)

/////////////////////////////////////////////////
//7E:FC00H-7E:FCFFH
/////////////////////////////////////////////////



/////////////////////////////////////////////////
//7E:FB00H-7E:FBFFH
/////////////////////////////////////////////////

#define     CHIPIDX                 ( (unsigned char volatile __xdata *)0x7efbd0)

#define     CHIPIDX0                (*(unsigned char volatile __xdata *)0x7efbd0)
#define     CHIPIDX1                (*(unsigned char volatile __xdata *)0x7efbd1)
#define     CHIPIDX2                (*(unsigned char volatile __xdata *)0x7efbd2)
#define     CHIPIDX3                (*(unsigned char volatile __xdata *)0x7efbd3)
#define     CHIPIDX4                (*(unsigned char volatile __xdata *)0x7efbd4)
#define     CHIPIDX5                (*(unsigned char volatile __xdata *)0x7efbd5)
#define     CHIPIDX6                (*(unsigned char volatile __xdata *)0x7efbd6)
#define     CHIPIDX7                (*(unsigned char volatile __xdata *)0x7efbd7)
#define     CHIPIDX8                (*(unsigned char volatile __xdata *)0x7efbd8)
#define     CHIPIDX9                (*(unsigned char volatile __xdata *)0x7efbd9)
#define     CHIPIDX10               (*(unsigned char volatile __xdata *)0x7efbda)
#define     CHIPIDX11               (*(unsigned char volatile __xdata *)0x7efbdb)
#define     CHIPIDX12               (*(unsigned char volatile __xdata *)0x7efbdc)
#define     CHIPIDX13               (*(unsigned char volatile __xdata *)0x7efbdd)
#define     CHIPIDX14               (*(unsigned char volatile __xdata *)0x7efbde)
#define     CHIPIDX15               (*(unsigned char volatile __xdata *)0x7efbdf)
#define     CHIPIDX16               (*(unsigned char volatile __xdata *)0x7efbe0)
#define     CHIPIDX17               (*(unsigned char volatile __xdata *)0x7efbe1)
#define     CHIPIDX18               (*(unsigned char volatile __xdata *)0x7efbe2)
#define     CHIPIDX19               (*(unsigned char volatile __xdata *)0x7efbe3)
#define     CHIPIDX20               (*(unsigned char volatile __xdata *)0x7efbe4)
#define     CHIPIDX21               (*(unsigned char volatile __xdata *)0x7efbe5)
#define     CHIPIDX22               (*(unsigned char volatile __xdata *)0x7efbe6)
#define     CHIPIDX23               (*(unsigned char volatile __xdata *)0x7efbe7)
#define     CHIPIDX24               (*(unsigned char volatile __xdata *)0x7efbe8)
#define     CHIPIDX25               (*(unsigned char volatile __xdata *)0x7efbe9)
#define     CHIPIDX26               (*(unsigned char volatile __xdata *)0x7efbea)
#define     CHIPIDX27               (*(unsigned char volatile __xdata *)0x7efbeb)
#define     CHIPIDX28               (*(unsigned char volatile __xdata *)0x7efbec)
#define     CHIPIDX29               (*(unsigned char volatile __xdata *)0x7efbed)
#define     CHIPIDX30               (*(unsigned char volatile __xdata *)0x7efbee)
#define     CHIPIDX31               (*(unsigned char volatile __xdata *)0x7efbef)

#define     HSPWMA_CFG              (*(unsigned char volatile __xdata *)0x7efbf0)
#define     HSPWMA_ADR              (*(unsigned char volatile __xdata *)0x7efbf1)
#define     HSPWMA_DAT              (*(unsigned char volatile __xdata *)0x7efbf2)
#define     HSPWMA_ADRH             (*(unsigned char volatile __xdata *)0x7efbf3)
#define     HSPWMB_CFG              (*(unsigned char volatile __xdata *)0x7efbf4)
#define     HSPWMB_ADR              (*(unsigned char volatile __xdata *)0x7efbf5)
#define     HSPWMB_DAT              (*(unsigned char volatile __xdata *)0x7efbf6)
#define     HSPWMB_ADRH             (*(unsigned char volatile __xdata *)0x7efbf7)
#define     HSSPI_CFG               (*(unsigned char volatile __xdata *)0x7efbf8)
#define     HSSPI_CFG2              (*(unsigned char volatile __xdata *)0x7efbf9)
#define     HSSPI_STA               (*(unsigned char volatile __xdata *)0x7efbfa)
#define     HSSPI_PSCR              (*(unsigned char volatile __xdata *)0x7efbfb)

/////////////////////////////////////////////////
//7E:FA00H-7E:FAFFH
/////////////////////////////////////////////////

#define     DMA_M2M_CFG             (*(unsigned char volatile __xdata *)0x7efa00)
#define     DMA_M2M_CR              (*(unsigned char volatile __xdata *)0x7efa01)
#define     DMA_M2M_STA             (*(unsigned char volatile __xdata *)0x7efa02)
#define     DMA_M2M_AMT             (*(unsigned char volatile __xdata *)0x7efa03)
#define     DMA_M2M_DONE            (*(unsigned char volatile __xdata *)0x7efa04)
#define     DMA_M2M_TXAH            (*(unsigned char volatile __xdata *)0x7efa05)
#define     DMA_M2M_TXAL            (*(unsigned char volatile __xdata *)0x7efa06)
#define     DMA_M2M_RXAH            (*(unsigned char volatile __xdata *)0x7efa07)
#define     DMA_M2M_RXAL            (*(unsigned char volatile __xdata *)0x7efa08)

#define     DMA_ADC_CFG             (*(unsigned char volatile __xdata *)0x7efa10)
#define     DMA_ADC_CR              (*(unsigned char volatile __xdata *)0x7efa11)
#define     DMA_ADC_STA             (*(unsigned char volatile __xdata *)0x7efa12)
#define     DMA_ADC_AMT             (*(unsigned char volatile __xdata *)0x7efa13)
#define     DMA_ADC_DONE            (*(unsigned char volatile __xdata *)0x7efa14)
#define     DMA_ADC_RXAH            (*(unsigned char volatile __xdata *)0x7efa17)
#define     DMA_ADC_RXAL            (*(unsigned char volatile __xdata *)0x7efa18)
#define     DMA_ADC_CFG2            (*(unsigned char volatile __xdata *)0x7efa19)
#define     DMA_ADC_CHSW0           (*(unsigned char volatile __xdata *)0x7efa1a)
#define     DMA_ADC_CHSW1           (*(unsigned char volatile __xdata *)0x7efa1b)
#define     DMA_ADC_ITVH            (*(unsigned char volatile __xdata *)0x7efa1e)
#define     DMA_ADC_ITVL            (*(unsigned char volatile __xdata *)0x7efa1f)

#define     DMA_SPI_CFG             (*(unsigned char volatile __xdata *)0x7efa20)
#define     DMA_SPI_CR              (*(unsigned char volatile __xdata *)0x7efa21)
#define     DMA_SPI_STA             (*(unsigned char volatile __xdata *)0x7efa22)
#define     DMA_SPI_AMT             (*(unsigned char volatile __xdata *)0x7efa23)
#define     DMA_SPI_DONE            (*(unsigned char volatile __xdata *)0x7efa24)
#define     DMA_SPI_TXAH            (*(unsigned char volatile __xdata *)0x7efa25)
#define     DMA_SPI_TXAL            (*(unsigned char volatile __xdata *)0x7efa26)
#define     DMA_SPI_RXAH            (*(unsigned char volatile __xdata *)0x7efa27)
#define     DMA_SPI_RXAL            (*(unsigned char volatile __xdata *)0x7efa28)
#define     DMA_SPI_CFG2            (*(unsigned char volatile __xdata *)0x7efa29)
#define     DMA_SPI_ITVH            (*(unsigned char volatile __xdata *)0x7efa2e)
#define     DMA_SPI_ITVL            (*(unsigned char volatile __xdata *)0x7efa2f)

#define     DMA_UR1T_CFG            (*(unsigned char volatile __xdata *)0x7efa30)
#define     DMA_UR1T_CR             (*(unsigned char volatile __xdata *)0x7efa31)
#define     DMA_UR1T_STA            (*(unsigned char volatile __xdata *)0x7efa32)
#define     DMA_UR1T_AMT            (*(unsigned char volatile __xdata *)0x7efa33)
#define     DMA_UR1T_DONE           (*(unsigned char volatile __xdata *)0x7efa34)
#define     DMA_UR1T_TXAH           (*(unsigned char volatile __xdata *)0x7efa35)
#define     DMA_UR1T_TXAL           (*(unsigned char volatile __xdata *)0x7efa36)
#define     DMA_UR1R_CFG            (*(unsigned char volatile __xdata *)0x7efa38)
#define     DMA_UR1R_CR             (*(unsigned char volatile __xdata *)0x7efa39)
#define     DMA_UR1R_STA            (*(unsigned char volatile __xdata *)0x7efa3a)
#define     DMA_UR1R_AMT            (*(unsigned char volatile __xdata *)0x7efa3b)
#define     DMA_UR1R_DONE           (*(unsigned char volatile __xdata *)0x7efa3c)
#define     DMA_UR1R_RXAH           (*(unsigned char volatile __xdata *)0x7efa3d)
#define     DMA_UR1R_RXAL           (*(unsigned char volatile __xdata *)0x7efa3e)

#define     DMA_UR2T_CFG            (*(unsigned char volatile __xdata *)0x7efa40)
#define     DMA_UR2T_CR             (*(unsigned char volatile __xdata *)0x7efa41)
#define     DMA_UR2T_STA            (*(unsigned char volatile __xdata *)0x7efa42)
#define     DMA_UR2T_AMT            (*(unsigned char volatile __xdata *)0x7efa43)
#define     DMA_UR2T_DONE           (*(unsigned char volatile __xdata *)0x7efa44)
#define     DMA_UR2T_TXAH           (*(unsigned char volatile __xdata *)0x7efa45)
#define     DMA_UR2T_TXAL           (*(unsigned char volatile __xdata *)0x7efa46)
#define     DMA_UR2R_CFG            (*(unsigned char volatile __xdata *)0x7efa48)
#define     DMA_UR2R_CR             (*(unsigned char volatile __xdata *)0x7efa49)
#define     DMA_UR2R_STA            (*(unsigned char volatile __xdata *)0x7efa4a)
#define     DMA_UR2R_AMT            (*(unsigned char volatile __xdata *)0x7efa4b)
#define     DMA_UR2R_DONE           (*(unsigned char volatile __xdata *)0x7efa4c)
#define     DMA_UR2R_RXAH           (*(unsigned char volatile __xdata *)0x7efa4d)
#define     DMA_UR2R_RXAL           (*(unsigned char volatile __xdata *)0x7efa4e)

#define     DMA_UR3T_CFG            (*(unsigned char volatile __xdata *)0x7efa50)
#define     DMA_UR3T_CR             (*(unsigned char volatile __xdata *)0x7efa51)
#define     DMA_UR3T_STA            (*(unsigned char volatile __xdata *)0x7efa52)
#define     DMA_UR3T_AMT            (*(unsigned char volatile __xdata *)0x7efa53)
#define     DMA_UR3T_DONE           (*(unsigned char volatile __xdata *)0x7efa54)
#define     DMA_UR3T_TXAH           (*(unsigned char volatile __xdata *)0x7efa55)
#define     DMA_UR3T_TXAL           (*(unsigned char volatile __xdata *)0x7efa56)
#define     DMA_UR3R_CFG            (*(unsigned char volatile __xdata *)0x7efa58)
#define     DMA_UR3R_CR             (*(unsigned char volatile __xdata *)0x7efa59)
#define     DMA_UR3R_STA            (*(unsigned char volatile __xdata *)0x7efa5a)
#define     DMA_UR3R_AMT            (*(unsigned char volatile __xdata *)0x7efa5b)
#define     DMA_UR3R_DONE           (*(unsigned char volatile __xdata *)0x7efa5c)
#define     DMA_UR3R_RXAH           (*(unsigned char volatile __xdata *)0x7efa5d)
#define     DMA_UR3R_RXAL           (*(unsigned char volatile __xdata *)0x7efa5e)

#define     DMA_UR4T_CFG            (*(unsigned char volatile __xdata *)0x7efa60)
#define     DMA_UR4T_CR             (*(unsigned char volatile __xdata *)0x7efa61)
#define     DMA_UR4T_STA            (*(unsigned char volatile __xdata *)0x7efa62)
#define     DMA_UR4T_AMT            (*(unsigned char volatile __xdata *)0x7efa63)
#define     DMA_UR4T_DONE           (*(unsigned char volatile __xdata *)0x7efa64)
#define     DMA_UR4T_TXAH           (*(unsigned char volatile __xdata *)0x7efa65)
#define     DMA_UR4T_TXAL           (*(unsigned char volatile __xdata *)0x7efa66)
#define     DMA_UR4R_CFG            (*(unsigned char volatile __xdata *)0x7efa68)
#define     DMA_UR4R_CR             (*(unsigned char volatile __xdata *)0x7efa69)
#define     DMA_UR4R_STA            (*(unsigned char volatile __xdata *)0x7efa6a)
#define     DMA_UR4R_AMT            (*(unsigned char volatile __xdata *)0x7efa6b)
#define     DMA_UR4R_DONE           (*(unsigned char volatile __xdata *)0x7efa6c)
#define     DMA_UR4R_RXAH           (*(unsigned char volatile __xdata *)0x7efa6d)
#define     DMA_UR4R_RXAL           (*(unsigned char volatile __xdata *)0x7efa6e)

#define     DMA_LCM_CFG             (*(unsigned char volatile __xdata *)0x7efa70)
#define     DMA_LCM_CR              (*(unsigned char volatile __xdata *)0x7efa71)
#define     DMA_LCM_STA             (*(unsigned char volatile __xdata *)0x7efa72)
#define     DMA_LCM_AMT             (*(unsigned char volatile __xdata *)0x7efa73)
#define     DMA_LCM_DONE            (*(unsigned char volatile __xdata *)0x7efa74)
#define     DMA_LCM_TXAH            (*(unsigned char volatile __xdata *)0x7efa75)
#define     DMA_LCM_TXAL            (*(unsigned char volatile __xdata *)0x7efa76)
#define     DMA_LCM_RXAH            (*(unsigned char volatile __xdata *)0x7efa77)
#define     DMA_LCM_RXAL            (*(unsigned char volatile __xdata *)0x7efa78)
#define     DMA_LCM_ITVH            (*(unsigned char volatile __xdata *)0x7efa7e)
#define     DMA_LCM_ITVL            (*(unsigned char volatile __xdata *)0x7efa7f)

#define     DMA_M2M_AMTH            (*(unsigned char volatile __xdata *)0x7efa80)
#define     DMA_M2M_DONEH           (*(unsigned char volatile __xdata *)0x7efa81)
#define     DMA_ADC_AMTH            (*(unsigned char volatile __xdata *)0x7efa82)
#define     DMA_ADC_DONEH           (*(unsigned char volatile __xdata *)0x7efa83)
#define     DMA_SPI_AMTH            (*(unsigned char volatile __xdata *)0x7efa84)
#define     DMA_SPI_DONEH           (*(unsigned char volatile __xdata *)0x7efa85)
#define     DMA_LCM_AMTH            (*(unsigned char volatile __xdata *)0x7efa86)
#define     DMA_LCM_DONEH           (*(unsigned char volatile __xdata *)0x7efa87)
#define     DMA_UR1T_AMTH           (*(unsigned char volatile __xdata *)0x7efa88)
#define     DMA_UR1T_DONEH          (*(unsigned char volatile __xdata *)0x7efa89)
#define     DMA_UR1R_AMTH           (*(unsigned char volatile __xdata *)0x7efa8a)
#define     DMA_UR1R_DONEH          (*(unsigned char volatile __xdata *)0x7efa8b)
#define     DMA_UR2T_AMTH           (*(unsigned char volatile __xdata *)0x7efa8c)
#define     DMA_UR2T_DONEH          (*(unsigned char volatile __xdata *)0x7efa8d)
#define     DMA_UR2R_AMTH           (*(unsigned char volatile __xdata *)0x7efa8e)
#define     DMA_UR2R_DONEH          (*(unsigned char volatile __xdata *)0x7efa8f)
#define     DMA_UR3T_AMTH           (*(unsigned char volatile __xdata *)0x7efa90)
#define     DMA_UR3T_DONEH          (*(unsigned char volatile __xdata *)0x7efa91)
#define     DMA_UR3R_AMTH           (*(unsigned char volatile __xdata *)0x7efa92)
#define     DMA_UR3R_DONEH          (*(unsigned char volatile __xdata *)0x7efa93)
#define     DMA_UR4T_AMTH           (*(unsigned char volatile __xdata *)0x7efa94)
#define     DMA_UR4T_DONEH          (*(unsigned char volatile __xdata *)0x7efa95)
#define     DMA_UR4R_AMTH           (*(unsigned char volatile __xdata *)0x7efa96)
#define     DMA_UR4R_DONEH          (*(unsigned char volatile __xdata *)0x7efa97)

#define     DMA_I2CT_CFG            (*(unsigned char volatile __xdata *)0x7efa98)
#define     DMA_I2CT_CR             (*(unsigned char volatile __xdata *)0x7efa99)
#define     DMA_I2CT_STA            (*(unsigned char volatile __xdata *)0x7efa9a)
#define     DMA_I2CT_AMT            (*(unsigned char volatile __xdata *)0x7efa9b)
#define     DMA_I2CT_DONE           (*(unsigned char volatile __xdata *)0x7efa9c)
#define     DMA_I2CT_TXAH           (*(unsigned char volatile __xdata *)0x7efa9d)
#define     DMA_I2CT_TXAL           (*(unsigned char volatile __xdata *)0x7efa9e)
#define     DMA_I2CR_CFG            (*(unsigned char volatile __xdata *)0x7efaa0)
#define     DMA_I2CR_CR             (*(unsigned char volatile __xdata *)0x7efaa1)
#define     DMA_I2CR_STA            (*(unsigned char volatile __xdata *)0x7efaa2)
#define     DMA_I2CR_AMT            (*(unsigned char volatile __xdata *)0x7efaa3)
#define     DMA_I2CR_DONE           (*(unsigned char volatile __xdata *)0x7efaa4)
#define     DMA_I2CR_RXAH           (*(unsigned char volatile __xdata *)0x7efaa5)
#define     DMA_I2CR_RXAL           (*(unsigned char volatile __xdata *)0x7efaa6)

#define     DMA_I2CT_AMTH           (*(unsigned char volatile __xdata *)0x7efaa8)
#define     DMA_I2CT_DONEH          (*(unsigned char volatile __xdata *)0x7efaa9)
#define     DMA_I2CR_AMTH           (*(unsigned char volatile __xdata *)0x7efaaa)
#define     DMA_I2CR_DONEH          (*(unsigned char volatile __xdata *)0x7efaab)
#define     DMA_I2C_CR              (*(unsigned char volatile __xdata *)0x7efaad)
#define     DMA_I2C_ST1             (*(unsigned char volatile __xdata *)0x7efaae)
#define     DMA_I2C_ST2             (*(unsigned char volatile __xdata *)0x7efaaf)

#define     DMA_I2ST_CFG            (*(unsigned char volatile __xdata *)0x7efab0)
#define     DMA_I2ST_CR             (*(unsigned char volatile __xdata *)0x7efab1)
#define     DMA_I2ST_STA            (*(unsigned char volatile __xdata *)0x7efab2)
#define     DMA_I2ST_AMT            (*(unsigned char volatile __xdata *)0x7efab3)
#define     DMA_I2ST_DONE           (*(unsigned char volatile __xdata *)0x7efab4)
#define     DMA_I2ST_TXAH           (*(unsigned char volatile __xdata *)0x7efab5)
#define     DMA_I2ST_TXAL           (*(unsigned char volatile __xdata *)0x7efab6)
#define     DMA_I2SR_CFG            (*(unsigned char volatile __xdata *)0x7efab8)
#define     DMA_I2SR_CR             (*(unsigned char volatile __xdata *)0x7efab9)
#define     DMA_I2SR_STA            (*(unsigned char volatile __xdata *)0x7efaba)
#define     DMA_I2SR_AMT            (*(unsigned char volatile __xdata *)0x7efabb)
#define     DMA_I2SR_DONE           (*(unsigned char volatile __xdata *)0x7efabc)
#define     DMA_I2SR_RXAH           (*(unsigned char volatile __xdata *)0x7efabd)
#define     DMA_I2SR_RXAL           (*(unsigned char volatile __xdata *)0x7efabe)

#define     DMA_I2ST_AMTH           (*(unsigned char volatile __xdata *)0x7efac0)
#define     DMA_I2ST_DONEH          (*(unsigned char volatile __xdata *)0x7efac1)
#define     DMA_I2SR_AMTH           (*(unsigned char volatile __xdata *)0x7efac2)
#define     DMA_I2SR_DONEH          (*(unsigned char volatile __xdata *)0x7efac3)
#define     DMA_I2C_ITVH            (*(unsigned char volatile __xdata *)0x7efac4)
#define     DMA_I2C_ITVL            (*(unsigned char volatile __xdata *)0x7efac5)
#define     DMA_I2S_ITVH            (*(unsigned char volatile __xdata *)0x7efac6)
#define     DMA_I2S_ITVL            (*(unsigned char volatile __xdata *)0x7efac7)
#define     DMA_UR1_ITVH            (*(unsigned char volatile __xdata *)0x7efac8)
#define     DMA_UR1_ITVL            (*(unsigned char volatile __xdata *)0x7efac9)
#define     DMA_UR2_ITVH            (*(unsigned char volatile __xdata *)0x7efaca)
#define     DMA_UR2_ITVL            (*(unsigned char volatile __xdata *)0x7efacb)
#define     DMA_UR3_ITVH            (*(unsigned char volatile __xdata *)0x7efacc)
#define     DMA_UR3_ITVL            (*(unsigned char volatile __xdata *)0x7efacd)
#define     DMA_UR4_ITVH            (*(unsigned char volatile __xdata *)0x7eface)
#define     DMA_UR4_ITVL            (*(unsigned char volatile __xdata *)0x7efacf)

#define     DMA_QSPI_CFG            (*(unsigned char volatile __xdata *)0x7efad0)
#define     DMA_QSPI_CR             (*(unsigned char volatile __xdata *)0x7efad1)
#define     DMA_QSPI_STA            (*(unsigned char volatile __xdata *)0x7efad2)
#define     DMA_QSPI_AMT            (*(unsigned char volatile __xdata *)0x7efad3)
#define     DMA_QSPI_DONE           (*(unsigned char volatile __xdata *)0x7efad4)
#define     DMA_QSPI_TXAH           (*(unsigned char volatile __xdata *)0x7efad5)
#define     DMA_QSPI_TXAL           (*(unsigned char volatile __xdata *)0x7efad6)
#define     DMA_QSPI_RXAH           (*(unsigned char volatile __xdata *)0x7efad7)
#define     DMA_QSPI_RXAL           (*(unsigned char volatile __xdata *)0x7efad8)
#define     DMA_QSPI_AMTH           (*(unsigned char volatile __xdata *)0x7efadb)
#define     DMA_QSPI_DONEH          (*(unsigned char volatile __xdata *)0x7efadc)
#define     DMA_QSPI_ITVH           (*(unsigned char volatile __xdata *)0x7efade)
#define     DMA_QSPI_ITVL           (*(unsigned char volatile __xdata *)0x7efadf)

#define     DMA_P2P_CR1             (*(unsigned char volatile __xdata *)0x7efaf0)
#define     DMA_P2P_CR2             (*(unsigned char volatile __xdata *)0x7efaf1)
#define     DMA_ARB_CFG             (*(unsigned char volatile __xdata *)0x7efaf8)
#define     DMA_ARB_STA             (*(unsigned char volatile __xdata *)0x7efaf9)

/////////////////////////////////////////////////
//7E:F900H-7E:F9FFH
/////////////////////////////////////////////////

#define     QSPI_CR1                (*(unsigned char volatile __xdata *)0x7ef900)
#define     QSPI_CR2                (*(unsigned char volatile __xdata *)0x7ef901)
#define     QSPI_CR3                (*(unsigned char volatile __xdata *)0x7ef902)
#define     QSPI_CR4                (*(unsigned char volatile __xdata *)0x7ef903)
#define     QSPI_DCR1               (*(unsigned char volatile __xdata *)0x7ef904)
#define     QSPI_DCR2               (*(unsigned char volatile __xdata *)0x7ef905)
#define     QSPI_SR1                (*(unsigned char volatile __xdata *)0x7ef906)
#define     QSPI_SR2                (*(unsigned char volatile __xdata *)0x7ef907)
#define     QSPI_FCR                (*(unsigned char volatile __xdata *)0x7ef908)
#define     QSPI_HCR1               (*(unsigned char volatile __xdata *)0x7ef909)
#define     QSPI_HCR2               (*(unsigned char volatile __xdata *)0x7ef90a)
#define     QSPI_DLR1               (*(unsigned char volatile __xdata *)0x7ef910)
#define     QSPI_DLR2               (*(unsigned char volatile __xdata *)0x7ef911)
#define     QSPI_CCR1               (*(unsigned char volatile __xdata *)0x7ef914)
#define     QSPI_CCR2               (*(unsigned char volatile __xdata *)0x7ef915)
#define     QSPI_CCR3               (*(unsigned char volatile __xdata *)0x7ef916)
#define     QSPI_CCR4               (*(unsigned char volatile __xdata *)0x7ef917)
#define     QSPI_AR1                (*(unsigned char volatile __xdata *)0x7ef918)
#define     QSPI_AR2                (*(unsigned char volatile __xdata *)0x7ef919)
#define     QSPI_AR3                (*(unsigned char volatile __xdata *)0x7ef91a)
#define     QSPI_AR4                (*(unsigned char volatile __xdata *)0x7ef91b)
#define     QSPI_ABR                (*(unsigned char volatile __xdata *)0x7ef91c)
#define     QSPI_DR                 (*(unsigned char volatile __xdata *)0x7ef920)
#define     QSPI_PSMKR1             (*(unsigned char volatile __xdata *)0x7ef924)
#define     QSPI_PSMAR1             (*(unsigned char volatile __xdata *)0x7ef928)
#define     QSPI_PIR1               (*(unsigned char volatile __xdata *)0x7ef92c)
#define     QSPI_PIR2               (*(unsigned char volatile __xdata *)0x7ef92d)

#define     PWMA_ENO2               (*(unsigned char volatile __xdata *)0x7ef930)
#define     PWMA_IOAUX2             (*(unsigned char volatile __xdata *)0x7ef931)
#define     PWMA_CR3                (*(unsigned char volatile __xdata *)0x7ef932)
#define     PWMA_SR3                (*(unsigned char volatile __xdata *)0x7ef933)
#define     PWMA_CCER3              (*(unsigned char volatile __xdata *)0x7ef934)
#define     PWMA_CCMR1X             (*(unsigned char volatile __xdata *)0x7ef938)
#define     PWMA_CCMR2X             (*(unsigned char volatile __xdata *)0x7ef939)
#define     PWMA_CCMR3X             (*(unsigned char volatile __xdata *)0x7ef93a)
#define     PWMA_CCMR4X             (*(unsigned char volatile __xdata *)0x7ef93b)
#define     PWMA_CCMR5              (*(unsigned char volatile __xdata *)0x7ef93c)
#define     PWMA_CCMR5X             (*(unsigned char volatile __xdata *)0x7ef93d)
#define     PWMA_CCMR6              (*(unsigned char volatile __xdata *)0x7ef93e)
#define     PWMA_CCMR6X             (*(unsigned char volatile __xdata *)0x7ef93f)
#define     PWMA_CCR5H              (*(unsigned char volatile __xdata *)0x7ef940)
#define     PWMA_CCR5L              (*(unsigned char volatile __xdata *)0x7ef941)
#define     PWMA_CCR5X              (*(unsigned char volatile __xdata *)0x7ef942)
#define     PWMA_CCR6H              (*(unsigned char volatile __xdata *)0x7ef943)
#define     PWMA_CCR6L              (*(unsigned char volatile __xdata *)0x7ef944)
#define     PWMA_DER                (*(unsigned char volatile __xdata *)0x7ef948)
#define     PWMA_DBA                (*(unsigned char volatile __xdata *)0x7ef949)
#define     PWMA_DBL                (*(unsigned char volatile __xdata *)0x7ef94a)
#define     PWMA_DMACR              (*(unsigned char volatile __xdata *)0x7ef94b)

#define     DMA_PWMAT_CFG           (*(unsigned char volatile __xdata *)0x7ef980)
#define     DMA_PWMAT_CR            (*(unsigned char volatile __xdata *)0x7ef981)
#define     DMA_PWMAT_STA           (*(unsigned char volatile __xdata *)0x7ef982)
#define     DMA_PWMAT_AMTH          (*(unsigned char volatile __xdata *)0x7ef984)
#define     DMA_PWMAT_AMT           (*(unsigned char volatile __xdata *)0x7ef985)
#define     DMA_PWMAT_DONEH         (*(unsigned char volatile __xdata *)0x7ef986)
#define     DMA_PWMAT_DONE          (*(unsigned char volatile __xdata *)0x7ef987)
#define     DMA_PWMAT_TXAH          (*(unsigned char volatile __xdata *)0x7ef988)
#define     DMA_PWMAT_TXAL          (*(unsigned char volatile __xdata *)0x7ef989)
#define     DMA_PWMA_ITVH           (*(unsigned char volatile __xdata *)0x7ef98e)
#define     DMA_PWMA_ITVL           (*(unsigned char volatile __xdata *)0x7ef98f)

#define     DMA_PWMAR_CFG           (*(unsigned char volatile __xdata *)0x7ef990)
#define     DMA_PWMAR_CR            (*(unsigned char volatile __xdata *)0x7ef991)
#define     DMA_PWMAR_STA           (*(unsigned char volatile __xdata *)0x7ef992)
#define     DMA_PWMAR_AMTH          (*(unsigned char volatile __xdata *)0x7ef994)
#define     DMA_PWMAR_AMT           (*(unsigned char volatile __xdata *)0x7ef995)
#define     DMA_PWMAR_DONEH         (*(unsigned char volatile __xdata *)0x7ef996)
#define     DMA_PWMAR_DONE          (*(unsigned char volatile __xdata *)0x7ef997)
#define     DMA_PWMAR_RXAH          (*(unsigned char volatile __xdata *)0x7ef998)
#define     DMA_PWMAR_RXAL          (*(unsigned char volatile __xdata *)0x7ef999)

/////////////////////////////////////////////////
//USB Control Regiter
/////////////////////////////////////////////////

#define     USBBASE                 0
#define     FADDR                   (USBBASE + 0)
#define     UPDATE                  0x80
#define     POWER                   (USBBASE + 1)
#define     ISOUD                   0x80
#define     USBRST                  0x08
#define     USBRSU                  0x04
#define     USBSUS                  0x02
#define     ENSUS                   0x01
#define     INTRIN1                 (USBBASE + 2)
#define     EP5INIF                 0x20
#define     EP4INIF                 0x10
#define     EP3INIF                 0x08
#define     EP2INIF                 0x04
#define     EP1INIF                 0x02
#define     EP0IF                   0x01
#define     INTROUT1                (USBBASE + 4)
#define     EP5OUTIF                0x20
#define     EP4OUTIF                0x10
#define     EP3OUTIF                0x08
#define     EP2OUTIF                0x04
#define     EP1OUTIF                0x02
#define     INTRUSB                 (USBBASE + 6)
#define     SOFIF                   0x08
#define     RSTIF                   0x04
#define     RSUIF                   0x02
#define     SUSIF                   0x01
#define     INTRIN1E                (USBBASE + 7)
#define     EP5INIE                 0x20
#define     EP4INIE                 0x10
#define     EP3INIE                 0x08
#define     EP2INIE                 0x04
#define     EP1INIE                 0x02
#define     EP0IE                   0x01
#define     INTROUT1E               (USBBASE + 9)
#define     EP5OUTIE                0x20
#define     EP4OUTIE                0x10
#define     EP3OUTIE                0x08
#define     EP2OUTIE                0x04
#define     EP1OUTIE                0x02
#define     INTRUSBE                (USBBASE + 11)
#define     SOFIE                   0x08
#define     RSTIE                   0x04
#define     RSUIE                   0x02
#define     SUSIE                   0x01
#define     FRAME1                  (USBBASE + 12)
#define     FRAME2                  (USBBASE + 13)
#define     INDEX                   (USBBASE + 14)
#define     INMAXP                  (USBBASE + 16)
#define     CSR0                    (USBBASE + 17)
#define     SSUEND                  0x80
#define     SOPRDY                  0x40
#define     SDSTL                   0x20
#define     SUEND                   0x10
#define     DATEND                  0x08
#define     STSTL                   0x04
#define     IPRDY                   0x02
#define     OPRDY                   0x01
#define     INCSR1                  (USBBASE + 17)
#define     INCLRDT                 0x40
#define     INSTSTL                 0x20
#define     INSDSTL                 0x10
#define     INFLUSH                 0x08
#define     INUNDRUN                0x04
#define     INFIFONE                0x02
#define     INIPRDY                 0x01
#define     INCSR2                  (USBBASE + 18)
#define     INAUTOSET               0x80
#define     INISO                   0x40
#define     INMODEIN                0x20
#define     INMODEOUT               0x00
#define     INENDMA                 0x10
#define     INFCDT                  0x08
#define     OUTMAXP                 (USBBASE + 19)
#define     OUTCSR1                 (USBBASE + 20)
#define     OUTCLRDT                0x80
#define     OUTSTSTL                0x40
#define     OUTSDSTL                0x20
#define     OUTFLUSH                0x10
#define     OUTDATERR               0x08
#define     OUTOVRRUN               0x04
#define     OUTFIFOFUL              0x02
#define     OUTOPRDY                0x01
#define     OUTCSR2                 (USBBASE + 21)
#define     OUTAUTOCLR              0x80
#define     OUTISO                  0x40
#define     OUTENDMA                0x20
#define     OUTDMAMD                0x10
#define     COUNT0                  (USBBASE + 22)
#define     OUTCOUNT1               (USBBASE + 22)
#define     OUTCOUNT2               (USBBASE + 23)
#define     FIFO0                   (USBBASE + 32)
#define     FIFO1                   (USBBASE + 33)
#define     FIFO2                   (USBBASE + 34)
#define     FIFO3                   (USBBASE + 35)
#define     FIFO4                   (USBBASE + 36)
#define     FIFO5                   (USBBASE + 37)
#define     UTRKCTL                 (USBBASE + 48)
#define     UTRKSTS                 (USBBASE + 49)

/////////////////////////////////////////////////
//Interrupt Vector
/////////////////////////////////////////////////

#define     INT0_VECTOR             0       //0003H
#define     TMR0_VECTOR             1       //000BH
#define     INT1_VECTOR             2       //0013H
#define     TMR1_VECTOR             3       //001BH
#define     UART1_VECTOR            4       //0023H
#define     ADC_VECTOR              5       //002BH
#define     LVD_VECTOR              6       //0033H
#define     PCA_VECTOR              7       //003BH
#define     UART2_VECTOR            8       //0043H
#define     SPI_VECTOR              9       //004BH
#define     INT2_VECTOR             10      //0053H
#define     INT3_VECTOR             11      //005BH
#define     TMR2_VECTOR             12      //0063H
#define     USER_VECTOR             13      //006BH
#define     INT4_VECTOR             16      //0083H
#define     UART3_VECTOR            17      //008BH
#define     UART4_VECTOR            18      //0093H
#define     TMR3_VECTOR             19      //009BH
#define     TMR4_VECTOR             20      //00A3H
#define     CMP_VECTOR              21      //00ABH
#define     I2C_VECTOR              24      //00C3H
#define     USB_VECTOR              25      //00CBH
#define     PWMA_VECTOR             26      //00D3H
#define     PWMB_VECTOR             27      //00DBH

#define     RTC_VECTOR              36      //0123H
#define     P0INT_VECTOR            37      //012BH
#define     P1INT_VECTOR            38      //0133H
#define     P2INT_VECTOR            39      //013BH
#define     P3INT_VECTOR            40      //0143H
#define     P4INT_VECTOR            41      //014BH
#define     P5INT_VECTOR            42      //0153H
#define     P6INT_VECTOR            43      //015BH
#define     P7INT_VECTOR            44      //0163H
#define     DMA_M2M_VECTOR          47      //017BH
#define     DMA_ADC_VECTOR          48      //0183H
#define     DMA_SPI_VECTOR          49      //018BH
#define     DMA_UR1T_VECTOR         50      //0193H
#define     DMA_UR1R_VECTOR         51      //019BH
#define     DMA_UR2T_VECTOR         52      //01A3H
#define     DMA_UR2R_VECTOR         53      //01ABH
#define     DMA_UR3T_VECTOR         54      //01B3H
#define     DMA_UR3R_VECTOR         55      //01BBH
#define     DMA_UR4T_VECTOR         56      //01C3H
#define     DMA_UR4R_VECTOR         57      //01CBH
#define     DMA_LCM_VECTOR          58      //01D3H
#define     LCM_VECTOR              59      //01DBH
#define     DMA_I2CT_VECTOR         60      //01E3H
#define     DMA_I2CR_VECTOR         61      //01EBH
#define     I2S_VECTOR              62      //01F3H
#define     DMA_I2ST_VECTOR         63      //01FBH
#define     DMA_I2SR_VECTOR         64      //0203H
#define     DMA_QSPI_VECTOR         65      //020BH
#define     QSPI_VECTOR             66      //0213H
#define     TMR11_VECTOR            67      //021BH
#define     DMA_PWMAT_VECTOR        72      //0243H
#define     DMA_PWMAR_VECTOR        73      //024BH

/////////////////////////////////////////////////

#define	EAXSFR()		EAXFR = 1		/* MOVX A,@DPTR/MOVX @DPTR,A指令的操作对象为扩展SFR(XSFR) */
#define	EAXRAM()		EAXFR = 0		/* MOVX A,@DPTR/MOVX @DPTR,A指令的操作对象为扩展RAM(XRAM) */

/////////////////////////////////////////////////
#define NOP1()  _nop_()
#define NOP2()  NOP1(),NOP1()
#define NOP3()  NOP2(),NOP1()
#define NOP4()  NOP3(),NOP1()
#define NOP5()  NOP4(),NOP1()
#define NOP6()  NOP5(),NOP1()
#define NOP7()  NOP6(),NOP1()
#define NOP8()  NOP7(),NOP1()
#define NOP9()  NOP8(),NOP1()
#define NOP10() NOP9(),NOP1()
#define NOP11() NOP10(),NOP1()
#define NOP12() NOP11(),NOP1()
#define NOP13() NOP12(),NOP1()
#define NOP14() NOP13(),NOP1()
#define NOP15() NOP14(),NOP1()
#define NOP16() NOP15(),NOP1()
#define NOP17() NOP16(),NOP1()
#define NOP18() NOP17(),NOP1()
#define NOP19() NOP18(),NOP1()
#define NOP20() NOP19(),NOP1()
#define NOP21() NOP20(),NOP1()
#define NOP22() NOP21(),NOP1()
#define NOP23() NOP22(),NOP1()
#define NOP24() NOP23(),NOP1()
#define NOP25() NOP24(),NOP1()
#define NOP26() NOP25(),NOP1()
#define NOP27() NOP26(),NOP1()
#define NOP28() NOP27(),NOP1()
#define NOP29() NOP28(),NOP1()
#define NOP30() NOP29(),NOP1()
#define NOP31() NOP30(),NOP1()
#define NOP32() NOP31(),NOP1()
#define NOP33() NOP32(),NOP1()
#define NOP34() NOP33(),NOP1()
#define NOP35() NOP34(),NOP1()
#define NOP36() NOP35(),NOP1()
#define NOP37() NOP36(),NOP1()
#define NOP38() NOP37(),NOP1()
#define NOP39() NOP38(),NOP1()
#define NOP40() NOP39(),NOP1()
#define NOP(N)  NOP##N()


/////////////////////////////////////////////////


#endif

