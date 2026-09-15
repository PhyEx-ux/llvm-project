/* sbit -> builtin unsigned char lvalue (README 3.1) */
#define BEEP __builtin_mcs251_bit_lvalue(0xCC)
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef N
#define N MCS251_SFRBIT(0xD1, 5)
/* official-header macros not provided by the compat headers (README 3.2) */
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
/* runtime printf family (validation/mcs251-runtime); fixed-arity ABI, putchar returns void */
int printf(const char *fmt, ...);
#include "intrins.h"
/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include "app_MatrixKey.h"

/*************** 功能说明 ****************

矩阵键盘扫描.

******************************************/



//========================================================================
//                               本地常量声明	
//========================================================================


//========================================================================
//                               本地变量声明
//========================================================================

u8  cntms=50;

u8  bKeyCode;           //按键键码
u8  bKeyDebounce;       //行列键盘变量
unsigned char fKeyOK,fKeyHold;    //按键标志

//========================================================================
//                               本地函数声明
//========================================================================

void MatrixKeyScan(void);

//========================================================================
//                            外部函数和变量声明
//========================================================================


/*****************************************************
    行列键扫描程序

    Y    P32      P33      P34      P35
          |        |        |        |
X         |        |        |        |
P36 ---- K00 ---- K01 ---- K02 ---- K03 ----
          |        |        |        |
P37 ---- K04 ---- K05 ---- K06 ---- K07 ----
          |        |        |        |
******************************************************/

//========================================================================
// 函数: Sample_MatrixKey
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2022-05-26
//========================================================================
void Sample_MatrixKey(void)
{
    if(cntms > 0)
    {
        cntms--;
    }
    else
    {
        PWMB_ENO = 0x00;    //PWM6关闭输出，蜂鸣器关闭
        BEEP = 1;           //蜂鸣器关闭
    }

    MatrixKeyScan();
    if(fKeyOK)  //有键按下
    {
        fKeyOK = 0;
        printf("KeyCode=%d\r\n",bKeyCode);
        PWMB_ENO = 0x04;    //PWM6使能输出，蜂鸣器响起
        cntms = 5;          //持续时间 5*10ms
        bKeyCode = 0;
    }
}

//========================================================================
// 函数: Sample_MatrixKey
// 描述: 用户应用程序.
// 参数: None.
// 返回: None.
// 版本: V1.0, 2025-12-26
//========================================================================
void MatrixKeyScan(void)
{
    u8 key;

    key = 0;
    P36 = 0;
    NOP40();
    key = (P3 & 0x3c) >> 2;
    P36 = 1;

    P37 = 0;
    NOP40();
    key |= (P3 & 0x3c) << 2;
    P37 = 1;
    
    key ^= 0xff;   //取反

    if(key != bKeyCode)
    {
        bKeyCode = key;
        bKeyDebounce = 10;  //10 * 10ms = 100ms
    }
    else
    {
        if(bKeyDebounce)
        {
            bKeyDebounce--;
            if(bKeyDebounce == 0)
            {
                if(bKeyCode)
                {
                    if(fKeyHold == 0)   //判断按键是否释放，避免重复触发按键动作，长按需要重复触发按键功能的话屏蔽这条指令
                    {
                        fKeyHold = 1;
                        fKeyOK = 1;
                    }
                }
                else
                {
                    fKeyHold = 0;
                }
            }
        }
    }
}
