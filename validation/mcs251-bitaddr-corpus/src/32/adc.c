#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef ADC_FLAG
#define ADC_FLAG MCS251_SFRBIT(0xBC, 5)
#undef ADC_START
#define ADC_START MCS251_SFRBIT(0xBC, 6)
#include "intrins.h"
/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

#include "adc.h"

/*************** 功能说明 ****************

查询法读一次ADC结果.

******************************************/


//========================================================================
// 函数: u16 Get_ADC12bitResult(u8 channel)
// 描述: 查询法读一次ADC结果.
// 参数: channel: 选择要转换的ADC.
// 返回: 12位ADC结果.
// 版本: V1.0, 2025-12-05
//========================================================================
u16 Get_ADC12bitResult(u8 channel)  //channel = 0~15
{
    ADC_CONTR = (ADC_CONTR & 0xf0) | channel; //设置ADC转换通道
    ADC_START = 1;//启动ADC转换
    _nop_();
    _nop_();
    _nop_();
    _nop_();

    while(ADC_FLAG == 0);   //wait for ADC finish
    ADC_FLAG = 0;     //清除ADC结束标志
    return  (((u16)ADC_RES << 8) | ADC_RESL);
}

