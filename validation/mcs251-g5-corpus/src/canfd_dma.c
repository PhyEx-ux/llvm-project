/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/

/*************  DMA使用介绍  **************

DMA_CAN1T_CFG.5(Tx_adr_align)=0 时，缓冲区内每帧数据顺序排列；
DMA_CAN1T_CFG.5(Tx_adr_align)=1 时，缓冲区内每帧数据按照 DMA_CAN1T_FRM 指定长度排列，
例如：DMA_CAN1T_FRM=32，那么第一帧数据存放在缓冲区BYTE[0]~BYTE[31]，第二帧数据存放在缓冲区BYTE[32]~BYTE[63]...

DMA_CAN1T_CFG.4(tx_dma_bw)=0 时，数据宽度8位，DMA传输总数(AMT)按照8位计算，缓冲区内每帧数据顺序排列；
DMA_CAN1T_CFG.4(tx_dma_bw)=1 时，数据宽度32位，DMA传输总数(AMT)按照32位计算，缓冲区内每帧数据起始地址需要32位对齐，
例如：DMA传输4帧数据，每帧数据长度分别是 16 字节、15字节、14字节、13字节，
数据宽度8位时，(16+15+14+13)-1=57，DMA_CAN1T_AMT = 57;
数据宽度32位时，每帧数据长度占用4个WORD，(4+4+4+4)-1=15，DMA_CAN1T_AMT = 15;

******************************************/
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"
#include <stdint.h>

#include "canfd_dma.h"
#include "canfd.h"

//========================================================================

unsigned char	Can1DmaTxFlag=0;
unsigned char	Can1DmaRxFlag=0;
unsigned char	Can2DmaTxFlag=0;
unsigned char	Can2DmaRxFlag=0;

uint8_t xdata DmaTxBuffer[256];
uint8_t xdata DmaRxBuffer[256];

//========================================================================
// 函数: void Can1DMA_Config(void)
// 描述: CAN1 DMA 功能配置.
// 参数: none.
// 返回: none.
// 版本: V1.0, 2025-12-23
//========================================================================
void Can1_DMA_Config(void)
{
	DMA_CAN1T_STA = 0x00;
#if(TX_ADDR_ALIGN==1)
	DMA_CAN1T_CFG = 0xb0;   //bit7 1:Enable Interrupt; bit4 tx_dma_bw 1:32bit 0:8bit
	DMA_CAN1T_FRM = 0x20;   //每帧长度
#else
	DMA_CAN1T_CFG = 0x90;   //bit7 1:Enable Interrupt; bit4 tx_dma_bw 1:32bit 0:8bit
#endif
    
    //8位模式，AMT=(byte number-1)
    //32位模式，AMT=(word number-1)
	DMA_CAN1T_AMT = 15;//57;    //设置传输总字节数(低8位)：n+1
	DMA_CAN1T_AMTH = 0;     //设置传输总字节数(高8位)：n+1
	DMA_CAN1T_TXAH = (u8)((u16)&DmaTxBuffer >> 8);  //发送数据存储地址
	DMA_CAN1T_TXAL = (u8)((u16)&DmaTxBuffer);
	DMA_CAN1T_CR = 0x80;    //bit7 1:使能 CAN1T_DMA, bit6 1:开始 CAN1T_DMA


	DMA_CAN1R_STA = 0x00;
#if(RX_ADDR_ALIGN==1)
	DMA_CAN1R_CFG = 0xb0;   //bit7 1:Enable Interrupt
	DMA_CAN1R_FRM = 0x20;   //
#else
	DMA_CAN1R_CFG = 0x90;   //bit7 1:Enable Interrupt
#endif
	DMA_CAN1R_AMT  = 15;//57;    //设置传输总字节数(低8位)：n+1
	DMA_CAN1R_AMTH = 0;     //设置传输总字节数(高8位)：n+1
	DMA_CAN1R_RXAH = (u8)((u16)&DmaRxBuffer >> 8);  //接收数据存储地址
	DMA_CAN1R_RXAL = (u8)((u16)&DmaRxBuffer);
	DMA_CAN1R_CR = 0xc1;    //bit7 1:使能 CAN1R_DMA, bit6 1:开始 CAN1R_DMA, bit0 1:清除 FIFO

    CAN1_AUX_CR = 0x11;

    DMA_CAN1_ITVL = 0x0f;   //设置传输间隔时间(低8位)
	DMA_CAN1_ITVH = 0x00;   //设置传输间隔时间(高8位)
}

//========================================================================
// 函数: void Can2DMA_Config(void)
// 描述: CAN2 DMA 功能配置.
// 参数: none.
// 返回: none.
// 版本: V1.0, 2025-12-23
//========================================================================
void Can2_DMA_Config(void)
{
	DMA_CAN2T_STA = 0x00;
#if(TX_ADDR_ALIGN==1)
	DMA_CAN2T_CFG = 0xb0;   //bit7 1:Enable Interrupt; bit4 tx_dma_bw 1:32bit 0:8bit
	DMA_CAN2T_FRM = 0x20;   //每帧长度
#else
	DMA_CAN2T_CFG = 0x90;   //bit7 1:Enable Interrupt; bit4 tx_dma_bw 1:32bit 0:8bit
#endif
    
    //8位模式，AMT=(byte number-1)
    //32位模式，AMT=(word number-1)
	DMA_CAN2T_AMT = 15;//57;    //设置传输总字节数(低8位)：n+1
	DMA_CAN2T_AMTH = 0;     //设置传输总字节数(高8位)：n+1
	DMA_CAN2T_TXAH = (u8)((u16)&DmaTxBuffer >> 8);  //发送数据存储地址
	DMA_CAN2T_TXAL = (u8)((u16)&DmaTxBuffer);
	DMA_CAN2T_CR = 0x80;    //bit7 1:使能 CAN2T_DMA, bit6 1:开始 CAN2T_DMA


	DMA_CAN2R_STA = 0x00;
#if(RX_ADDR_ALIGN==1)
	DMA_CAN2R_CFG = 0xb0;   //bit7 1:Enable Interrupt
	DMA_CAN2R_FRM = 0x20;   //
#else
	DMA_CAN2R_CFG = 0x90;   //bit7 1:Enable Interrupt
#endif
	DMA_CAN2R_AMT  = 15;//57;    //设置传输总字节数(低8位)：n+1
	DMA_CAN2R_AMTH = 0;     //设置传输总字节数(高8位)：n+1
	DMA_CAN2R_RXAH = (u8)((u16)&DmaRxBuffer >> 8);  //接收数据存储地址
	DMA_CAN2R_RXAL = (u8)((u16)&DmaRxBuffer);
	DMA_CAN2R_CR = 0xc1;    //bit7 1:使能 CAN2R_DMA, bit6 1:开始 CAN2R_DMA, bit0 1:清除 FIFO

    CAN2_AUX_CR = 0x11;

    DMA_CAN2_ITVL = 0x0f;   //设置传输间隔时间(低8位)
	DMA_CAN2_ITVH = 0x00;   //设置传输间隔时间(高8位)
}

//========================================================================
void CANFD_Set_DMA_Buff(const stc_can_tx_t *pcanTx, uint8_t xdata *u8DmaTxBuf)
{
    uint8_t i;

    u8DmaTxBuf[0] = ((uint8_t *)&pcanTx->u32ID)[3];  //ID
    u8DmaTxBuf[1] = ((uint8_t *)&pcanTx->u32ID)[2];
    u8DmaTxBuf[2] = ((uint8_t *)&pcanTx->u32ID)[1];
    u8DmaTxBuf[3] = ((uint8_t *)&pcanTx->u32ID)[0];
    u8DmaTxBuf[4] = pcanTx->TxCtrl.u8Ctrl;          //CTRL
    for(i=0;i<pcanTx->TxCtrl.tx_ctrl.DLC;i++)
    {
        u8DmaTxBuf[8 + i] = pcanTx->pu8Data[i];     //Data
    }
}

//========================================================================
void CANFD_DMA_LoadData(void)
{
    uint8_t i;
    stc_can_tx_t pstcTx;

    pstcTx.u32ID = 0x01234567;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x10+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[0]);//数据写入缓冲区，总共 8+DLC 字节

    //tx_dma_bw = 1 时，地址需要32位对齐；tx_dma_bw = 0 时，数据顺序排列
#if(TX_ADDR_ALIGN==0)

    pstcTx.u32ID = 0x789;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_5;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 0;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x20+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[16]);//数据写入缓冲区，总共 8+DLC 字节。tx_dma_bw = 1 时，地址需要32位对齐；tx_dma_bw = 0 时，数据顺序排列

    pstcTx.u32ID = 0x3456789;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_6;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x30+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[32]);//29//数据写入缓冲区，总共 8+DLC 字节。tx_dma_bw = 1 时，地址需要32位对齐；tx_dma_bw = 0 时，数据顺序排列

    pstcTx.u32ID = 0x246;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_7;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 0;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x40+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[48]);//43//数据写入缓冲区，总共 8+DLC 字节。tx_dma_bw = 1 时，地址需要32位对齐；tx_dma_bw = 0 时，数据顺序排列

#else

    pstcTx.u32ID = 0x789;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_5;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 0;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x20+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[32]);//数据写入缓冲区，总共 8+DLC 字节，32bit是 4个word

    pstcTx.u32ID = 0x3456789;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_6;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x30+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[64]);//数据写入缓冲区，总共 8+DLC 字节，32bit是 4个word

    pstcTx.u32ID = 0x246;
    pstcTx.TxCtrl.u8Ctrl = 0x00U;
    pstcTx.TxCtrl.tx_ctrl.FDF = 0U;         //0:CAN2.0; 1:CANFD
    pstcTx.TxCtrl.tx_ctrl.BRS = 0U;         //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    pstcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_7;  //DLC:数据长度
    pstcTx.TxCtrl.tx_ctrl.IDE = 0;          //0:标准帧; 1:扩展帧

    for(i=0;i<pstcTx.TxCtrl.tx_ctrl.DLC;i++)
    {
        pstcTx.pu8Data[i] = 0x40+i;    //Data
    }
    CANFD_Set_DMA_Buff(&pstcTx,&DmaTxBuffer[96]);//数据写入缓冲区，总共 8+DLC 字节，32bit是 4个word

#endif
}

//========================================================================
// 函数: void CAN1_DMA_Interrupt (void) interrupt 117/118
// 描述: CAN1 DMA中断函数
// 参数: none.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-12-8
// 备注: 
//========================================================================
void __attribute__((interrupt(117))) CAN1T_DMA_Interrupt(void)
{
	if(DMA_CAN1T_STA & 0x01)   //发送完成
	{
		DMA_CAN1T_STA &= ~0x01;  //清除标志位
		Can1DmaTxFlag = 1;
	}
	if(DMA_CAN1T_STA & 0x04)   //数据覆盖
	{
		DMA_CAN1T_STA &= ~0x04;  //清除标志位
	}
}

void __attribute__((interrupt(118))) CAN1R_DMA_Interrupt(void)
{
	if(DMA_CAN1R_STA & 0x01)   //接收完成
	{
		DMA_CAN1R_STA &= ~0x01;  //清除标志位
		Can1DmaRxFlag = 1;
	}
	if(DMA_CAN1R_STA & 0x02)   //数据丢弃
	{
		DMA_CAN1R_STA &= ~0x02;  //清除标志位
	}
}

//========================================================================
// 函数: void CAN2_DMA_Interrupt (void) interrupt 119/120
// 描述: CAN2 DMA中断函数
// 参数: none.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-12-8
// 备注: 
//========================================================================
void __attribute__((interrupt(119))) CAN2T_DMA_Interrupt(void)
{
	if(DMA_CAN2T_STA & 0x01)   //发送完成
	{
		DMA_CAN2T_STA &= ~0x01;  //清除标志位
		Can2DmaTxFlag = 1;
	}
	if(DMA_CAN2T_STA & 0x04)   //数据覆盖
	{
		DMA_CAN2T_STA &= ~0x04;  //清除标志位
	}
}

void __attribute__((interrupt(120))) CAN2R_DMA_Interrupt(void)
{
	if(DMA_CAN2R_STA & 0x01)   //接收完成
	{
		DMA_CAN2R_STA &= ~0x01;  //清除标志位
		Can2DmaRxFlag = 1;
	}
	if(DMA_CAN2R_STA & 0x02)   //数据丢弃
	{
		DMA_CAN2R_STA &= ~0x02;  //清除标志位
	}
}

//========================================================================
