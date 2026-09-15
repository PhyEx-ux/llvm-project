/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/* --- BBS: www.STCAIMCU.com  -----------------------------------------*/
/*---------------------------------------------------------------------*/

#ifndef _CAN_FD_DMA_H
#define _CAN_FD_DMA_H
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"


#define TX_ADDR_ALIGN    1
#define RX_ADDR_ALIGN    1

#define DMA_CAN1T_Trig()  DMA_CAN1T_CR |= 0x40
#define DMA_CAN1R_Trig()  DMA_CAN1R_CR |= 0x40
#define DMA_CAN2T_Trig()  DMA_CAN2T_CR |= 0x40
#define DMA_CAN2R_Trig()  DMA_CAN2R_CR |= 0x40

/*******************************************************************************
 * Global variable definitions ('extern')
 ******************************************************************************/

extern unsigned char Can1DmaTxFlag;
extern unsigned char Can1DmaRxFlag;
extern unsigned char Can2DmaTxFlag;
extern unsigned char Can2DmaRxFlag;

extern uint8_t xdata DmaTxBuffer[256];
extern uint8_t xdata DmaRxBuffer[256];

/*******************************************************************************
  Global function prototypes (definition in C source)
 ******************************************************************************/

void Can1_DMA_Config(void);
void Can2_DMA_Config(void);
void CANFD_DMA_LoadData(void);

#endif