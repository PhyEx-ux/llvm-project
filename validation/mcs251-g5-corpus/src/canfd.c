/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/*---------------------------------------------------------------------*/
#include "mcs251_type_compat.h"
#include "stc32g144k246-v1.h"
#include "mcs251_bit_compat.h"
/* demo-local SFR bit shims (README 3.2): the compat header rejects these
   names; the base SFR byte is bit-addressable per the STC32G manual. */
#define MCS251_SFRBIT(BASE, N) \
    (*(volatile struct { unsigned char b7:1,b6:1,b5:1,b4:1,b3:1,b2:1,b1:1,b0:1; } *)(BASE)).b##N
#undef CAN2EN
#define CAN2EN MCS251_SFRBIT(0x97, 2)
#undef CAN2_S0
#define CAN2_S0 MCS251_SFRBIT(0xBB, 0)
#undef CAN2_S1
#define CAN2_S1 MCS251_SFRBIT(0xBB, 1)
#undef CANEDIN
#define CANEDIN MCS251_SFRBIT(0x97, 3)
#undef CANEN
#define CANEN MCS251_SFRBIT(0x97, 1)
#undef CAN_S0
#define CAN_S0 MCS251_SFRBIT(0xA2, 4)
#undef CAN_S1
#define CAN_S1 MCS251_SFRBIT(0xA2, 5)
/* official SFRs omitted by the port header (README 3.2) */
#define CANICR (*(volatile unsigned char *)0xF1)
#include <stdint.h>

#include "canfd.h"

//========================================================================

/**
 * @defgroup CAN_Configuration_Bit_Mask CAN Configuration Bit Mask
 * @{
 */
#define CAN_LB_MODE_MSK                     (CAN_CFG_STAT_LBMI | CAN_CFG_STAT_LBME)
#define CAN_TRANS_MODE_MSK                  (CAN_CFG_STAT_TPSS | CAN_CFG_STAT_TSSS)
#define CAN_TTC_FLAG_CLR_MSK                (CAN_TTC_FLAG_TTI | CAN_TTC_FLAG_WTI)
/**
 * @}
 */

stc_can_af_cfg_t astcAFCfg[] = { \
    {APP_CAN_AF1_ID, APP_CAN_AF1_ID_MSK, APP_CAN_AF1_MSK_TYPE}, \
    {APP_CAN_AF2_ID, APP_CAN_AF2_ID_MSK, APP_CAN_AF2_MSK_TYPE}, \
    {APP_CAN_AF3_ID, APP_CAN_AF3_ID_MSK, APP_CAN_AF3_MSK_TYPE}, \
    {APP_CAN_AF4_ID, APP_CAN_AF4_ID_MSK, APP_CAN_AF4_MSK_TYPE}, \
    {APP_CAN_AF5_ID, APP_CAN_AF5_ID_MSK, APP_CAN_AF5_MSK_TYPE}, \
    {APP_CAN_AF6_ID, APP_CAN_AF6_ID_MSK, APP_CAN_AF6_MSK_TYPE}, \
    {APP_CAN_AF7_ID, APP_CAN_AF7_ID_MSK, APP_CAN_AF7_MSK_TYPE}, \
    {APP_CAN_AF8_ID, APP_CAN_AF8_ID_MSK, APP_CAN_AF8_MSK_TYPE}, \
    {APP_CAN_AF9_ID, APP_CAN_AF9_ID_MSK, APP_CAN_AF9_MSK_TYPE}, \
    {APP_CAN_AF10_ID, APP_CAN_AF10_ID_MSK, APP_CAN_AF10_MSK_TYPE}, \
    {APP_CAN_AF11_ID, APP_CAN_AF11_ID_MSK, APP_CAN_AF11_MSK_TYPE}, \
    {APP_CAN_AF12_ID, APP_CAN_AF12_ID_MSK, APP_CAN_AF12_MSK_TYPE}, \
    {APP_CAN_AF13_ID, APP_CAN_AF13_ID_MSK, APP_CAN_AF13_MSK_TYPE}, \
    {APP_CAN_AF14_ID, APP_CAN_AF14_ID_MSK, APP_CAN_AF14_MSK_TYPE}, \
    {APP_CAN_AF15_ID, APP_CAN_AF15_ID_MSK, APP_CAN_AF15_MSK_TYPE}, \
    {APP_CAN_AF16_ID, APP_CAN_AF16_ID_MSK, APP_CAN_AF16_MSK_TYPE}, \
};

//========================================================================

unsigned char B_CanRead;     //CAN1 收到数据标志
unsigned char B_CanSend;     //CAN1 发送数据标志
unsigned char B_Can2Read;    //CAN2 收到数据标志
unsigned char B_Can2Send;    //CAN2 发送数据标志

//========================================================================
#if(SET_ENDIAN == 0)
uint16_t reverse2(uint16_t w)
{
    uint16_t ret;
    
    ((uint8_t *)&ret)[0] = ((uint8_t *)&w)[1];
    ((uint8_t *)&ret)[1] = ((uint8_t *)&w)[0];

    return ret;
}

//uint32_t reverse4(uint32_t d)
//{   
//    uint32_t ret;
//    
//    ((uint8_t *)&ret)[0] = ((uint8_t *)&d)[3];
//    ((uint8_t *)&ret)[1] = ((uint8_t *)&d)[2];
//    ((uint8_t *)&ret)[2] = ((uint8_t *)&d)[1];
//    ((uint8_t *)&ret)[3] = ((uint8_t *)&d)[0];

//    return ret;
//}
#endif

/**
 * @brief  Software reset the specified CAN unit. \
           Software reset is a partial reset and CANNOT reset all registers. \
           Some registers need software reset before written.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @retval None
 */
void CAN_SWReset(CANFD_TypeDef xdata* CANx)
{
    CANFD_SET_REG8_BIT(CANx->CFG_STAT, CAN_CFG_STAT_RESET);
}

/**
 * @brief  Set the CAN node to enter the normal communication mode.
 *         When this state is set, it takes 11 CAN bit time for this node to participate in communication.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @retval None
 */
void CAN_EnterNormalComm(CANFD_TypeDef xdata* CANx)
{
    CANFD_CLEAR_REG8_BIT(CANx->CFG_STAT, CAN_CFG_STAT_RESET);
}

/**
 * @brief  Specifies work mode for the specified CAN unit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8WorkMode             Work mode of CAN.
 *                                      This parameter can be a value of @ref CAN_Work_Mode
 *   @arg  CAN_MODE_NORMAL:             Normal work mode.
 *   @arg  CAN_MODE_SILENT:             Silent work mode. Prohibit transmission.
 *   @arg  CAN_MODE_ILB:                Internal loopback mode, just for self-test while developing.
 *   @arg  CAN_MODE_ELB:                External loopback mode, just for self-test while developing.
 *   @arg  CAN_MODE_ELB_SILENT:         External loppback silent mode, just for self-test while developing. \
 *                                      It is forbidden to respond to received frames and error frames, but can be transmitted.
 * @retval None
 * @note Call this function when CFG_STAT.RESET is 0.
 */
void CAN_SetWorkMode(CANFD_TypeDef xdata* CANx, uint8_t u8WorkMode)
{
    uint8_t u8CFGSTAT = 0U;
    uint8_t u8TCMD    = 0U;

    switch (u8WorkMode)
    {
        case CAN_MODE_SILENT:
            u8TCMD    = CAN_TCMD_LOM;
            break;
        case CAN_MODE_ILB:
            u8CFGSTAT = CAN_CFG_STAT_LBMI;
            break;
        case CAN_MODE_ELB:
            u8CFGSTAT = CAN_CFG_STAT_LBME;
            break;
        case CAN_MODE_ELB_SILENT:
            u8TCMD    = CAN_TCMD_LOM;
            u8CFGSTAT = CAN_CFG_STAT_LBME;
            break;
        case CAN_MODE_NORMAL:
        default:
            break;
    }

    CANFD_MODIFY_REG8(CANx->CFG_STAT, CAN_LB_MODE_MSK, u8CFGSTAT);
    CANFD_MODIFY_REG8(CANx->TCMD, CAN_TCMD_LOM, u8TCMD);
}

/**
 * @brief  Specifies transmission mode for the specified CAN unit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8TransMode            PTB/STB transmission mode.
 *                                      This parameter can be a value of @ref CAN_Trans_Mode
 *   @arg  CAN_TRANS_PTB_STB_AUTO_RETX: Both PTB and STB automatically retransmit.
 *   @arg  CAN_TRANS_PTB_SSHOT:         PTB single shot transmit.
 *   @arg  CAN_TRANS_STB_SSHOT:         STB single shot transmit.
 *   @arg  CAN_TRANS_PTB_STB_SSHOT:     STB and PTB both single shot transmit.
 * @retval None
 * @note Call this function when CFG_STAT.RESET is 0.
 */
void CAN_SetTransMode(CANFD_TypeDef xdata* CANx, uint8_t u8TransMode)
{
    CANFD_MODIFY_REG8(CANx->CFG_STAT, CAN_TRANS_MODE_MSK, u8TransMode);
}

/**
 * @brief  Specifies STB transmission priority mode for the specified CAN unit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8STBPrioMode          STB transmission priority mode.
 *                                      This parameter can be a value of @ref CAN_STB_Priority_Mode
 *   @arg  CAN_STB_PRIO_FIFO:           Data first in first be transmitted.
 *   @arg  CAN_STB_PRIO_ID :            Data with smallest ID first be transmitted.
 * @retval None
 * @note Whatever the priority mode of STB is, PTB always has the highest priority.
 */
/* G5 xdata-param: not in demo-82 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
void CAN_SetSTBPrioMode(CANFD_TypeDef *CANx, uint8_t u8STBPrioMode)
{
    CANFD_MODIFY_REG8(CANx->TCTRL, CAN_TCTRL_TSMODE, u8STBPrioMode);
}

/**
 * @brief  Specifies the receive buffer store selection for specified CAN unit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8RBStoreSel           Receive buffer store selection.
 *                                      This parameter can be a value of @ref CAN_RB_Store_Selection
 *   @arg  CAN_RB_STORE_CORRECT_DATA:   Receive buffer stores correct frames only.
 *   @arg  CAN_RB_STORE_ALL_DATA:       Receive buffer stores all frames, includes error .
 * @retval None
 * @note Call this function when CFG_STAT.RESET is 0.
 */
void CAN_SetRBStoreSel(CANFD_TypeDef xdata* CANx, uint8_t u8RBStoreSel)
{
    CANFD_MODIFY_REG8(CANx->RCTRL, CAN_RCTRL_RBALL, u8RBStoreSel);
}

/**
 * @brief  Specifies the operation when receiving buffer overflow.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8RBOvfOperation       Operation when receive buffer overflow.
 *                                      This parameter can be a value of @ref CAN_RB_Overflow_Operation
 *   @arg  CAN_RB_OVF_SAVE_NEW:         Saves the newly received and the first received will be overwritten.
 *   @arg  CAN_RB_OVF_DISCARD_NEW:      Discard the newly received .
 * @retval None
 */
void CAN_SetRBOvfOp(CANFD_TypeDef xdata* CANx, uint8_t u8RBOvfOperation)
{
    CANFD_MODIFY_REG8(CANx->RCTRL, CAN_RCTRL_ROM, u8RBOvfOperation);
}

/**
 * @brief  Enable or disable the specified interrupts of the specified CAN unit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u32IntType             Interrupt type of CAN. Set this parameter to 0xFFFFFFFF to select all the interrupts of CAN.
 *                                      This parameter can be values of @ref CAN_Interrupt_Type
 *   @arg  CAN_INT_ERR_INT:             Register bit RTIE.EIE. Error interrupt.
 *   @arg  CAN_INT_STB_TRANS_OK:        Register bit RTIE.TSIE. Secondary transmit buffer was transmitted successfully.
 *   @arg  CAN_INT_PTB_TRANS_OK:        Register bit RTIE.TPIE. Primary transmit buffer was transmitted successfully.
 *   @arg  CAN_INT_RB_ALMOST_FULL:      Register bit RTIE.RAFIE. The number of filled RB slot is greater than or equal to the LIMIT.AFWL setting value.
 *   @arg  CAN_INT_RB_FIFO_FULL:        Register bit RTIE.RFIE. The FIFO of receive buffer is full.
 *   @arg  CAN_INT_RX_OVERRUN:          Register bit RTIE.ROIE. Receive buffers are full and there is a further message to be stored.
 *   @arg  CAN_INT_RX:                  Register bit RTIE.RIE. Received a valid frame or remote frame.
 *   @arg  CAN_INT_BUS_ERR:             Register bit ERRINT.BEIE. Arbitration lost caused bus error
 *   @arg  CAN_INT_ARB_LOST:            Register bit ERRINT.ALIE. Arbitration lost.
 *   @arg  CAN_INT_ERR_PASSIVE:         Register bit ERRINT.EPIE. A change from error-passive to error-active or error-active to error-passive has occurred.
 * @param  [in]  enNewState             An en_functional_state_t enumeration type value.
 *   @arg  Enable:                      Enable the specified interrupts.
 *   @arg  Disable:                     Disable the specified interrupts.
 * @retval None
 */
void CAN_IntCmd(CANFD_TypeDef xdata* CANx, uint32_t u32IntType, en_functional_state_t enNewState)
{
    uint8_t u8RTIE;
    uint8_t u8ERRINT;

    u32IntType &= CAN_INT_ALL;
    u8RTIE      = (uint8_t)u32IntType;
    u8ERRINT    = (uint8_t)(u32IntType >> 8U);

    if (enNewState == Enable)
    {
        CANFD_SET_REG8_BIT(CANx->RTIE, u8RTIE);
        CANFD_SET_REG8_BIT(CANx->ERRINT, u8ERRINT);
    }
    else
    {
        CANFD_CLEAR_REG8_BIT(CANx->RTIE, u8RTIE);
        CANFD_CLEAR_REG8_BIT(CANx->ERRINT, u8ERRINT);
    }
}

/**
 * @brief  Configures slow unsigned char timing(SBT).
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  pstcCfg                Pointer to a stc_can_bt_cfg_t structure value that
 *                                      contains the configuration information for SBT.
 *   @arg  u32SEG1:                     TQs of segment 1. Contains synchronization segment, \
 *                                      propagation time segment and phase buffer segment 1.
 *   @arg  u32SEG2:                     TQs of segment 2. Phase buffer segment 2.
 *   @arg  u32SJW:                      TQs of synchronization jump width.
 *   @arg  u32Prescaler:                Range [1, 256].
 * @param  [in]  enCANFDCmd             Is CAN-FD enable?
 *   @arg Enable:                       CAN-FD is enable.
 *   @arg Disable:                      CAN-FD is disable.
 * @retval An en_result_t enumeration type value.
 *   @arg  Ok:                          No error occurred.
 *   @arg  ErrorInvalidParameter:       pstcCfg == NULL.
 * @note 1. Restrictions: u32SEG1 >= u32SEG2 + 1, u32SEG2 >= u32SJW.
 * @note 2. TQ = u32Prescaler / CANClock.
 * @note 3. Slow unsigned char time = (u32SEG1 + u32SEG2) * TQ.
 * @note 4. Call this function when CFG_STAT.RESET is 1.
 */
uint8_t CAN_SBTConfig(CANFD_TypeDef xdata* CANx, const stc_can_bt_cfg_t *pstcCfg)
{
    uint8_t enRet = 0;

    if (pstcCfg != NULL)
    {
        CANFD_WRITE_REG8(CANx->S_SEG1, pstcCfg->u8SEG1);
        CANFD_WRITE_REG8(CANx->S_SEG2, pstcCfg->u8SEG2);
        CANFD_WRITE_REG8(CANx->S_SJW, pstcCfg->u8SJW);
        CANFD_WRITE_REG8(CANx->S_PRESC, pstcCfg->u8Prescaler);

        enRet = 1;
    }

    return enRet;
}

/**
 * @brief  Clear the common flag's status.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u32Flag                Status flag. Set this parameter to 0xFFFFFFFF to select all sataus flags of CAN.
 *                                      This parameter can be a value of @ref CAN_Common_Status_Flag
 *   @arg  CAN_FLAG_RB_OVF:             Register bit RCTRL.ROV. Receive buffer is full and there is a further bit to be stored. At least one is lost.
 *   @arg  CAN_FLAG_TRANS_ABORTED:      Register bit RTIF.AIF. Transmit messages requested via TCMD.TPA and TCMD.TSA were successfully canceled.
 *   @arg  CAN_FLAG_ERR_INT:            Register bit RTIF.EIF. The CFG_STAT.BUSOFF unsigned char changes, or the relative relationship between the value of the error counter \
 *                                      and the set value of the ERROR warning limit changes. For example, the value of the error counter changes from less than \
 *                                      the set value to greater than the set value, or from greater than the set value to less than the set value.
 *   @arg  CAN_FLAG_STB_TRANS_OK:       Register bit RTIF.TSIF. STB was transmitted successfully.
 *   @arg  CAN_FLAG_PTB_TRANS_OK:       Register bit RTIF.TPIF. PTB was transmitted successfully.
 *   @arg  CAN_FLAG_RB_ALMOST_FULL:     Register bit RTIF.RAFIF. The number of filled RB slot is greater than or equal to the LIMIT.AFWL setting value.
 *   @arg  CAN_FLAG_RB_FIFO_FULL:       Register bit RTIF.RFIF. The FIFO of receive buffer is full.
 *   @arg  CAN_FLAG_RX_OVERRUN:         Register bit RTIF.ROIF. Receive buffers are all full and there is a further message to be stored.
 *   @arg  CAN_FLAG_RX_OK:              Register bit RTIF.RIF. Received a valid frame or remote frame.
 *   @arg  CAN_FLAG_BUS_ERR:            Register bit ERRINT.BEIF. Arbitration lost caused bus error.
 *   @arg  CAN_FLAG_ARB_LOST:           Register bit ERRINT.ALIF. Arbitration lost.
 *   @arg  CAN_FLAG_ERR_PASSIVE:        Register bit ERRINT.EPIF. A change from error-passive to error-active or error-active to error-passive has occurred.
 *   @arg  CAN_FLAG_ERR_PASSIVE_NODE:   Register bit ERRINT.EPASS. The node is an error-passive node.
 *   @arg  CAN_FLAG_REACH_WARN_LIMIT:   Register bit ERRINT.EWARN. REC or TEC is greater than or equal to the LIMIT.EWL setting value.
 * @retval None
 */
void CAN_ClrStatus(CANFD_TypeDef xdata* CANx, uint32_t u32Flag)
{
    uint8_t u8RTIF;
    uint8_t u8ERRINT;
    uint8_t u8Reg;

    u32Flag &= CAN_FLAG_CLR_MSK;
    u8RTIF   = (uint8_t)(u32Flag >> 16U);
    u8ERRINT = (uint8_t)(u32Flag >> 24U);

    if ((u32Flag & CAN_FLAG_RB_OVF) != 0U)
    {
        CANFD_SET_REG8_BIT(CANx->RCTRL, CAN_RCTRL_RREL);
    }

    CANFD_WRITE_REG8(CANx->RTIF, u8RTIF);

    u8Reg  = CANFD_READ_REG8(CANx->ERRINT);
    u8Reg &= 0x2AU;
    u8Reg |= u8ERRINT;
    CANFD_WRITE_REG8(CANx->ERRINT, u8Reg);
}

/**
 * @brief  Set receive buffer slots full warning limit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in] u8RBSWarnLimit:         Receive buffer slots full warning limit. Rang is [1, 8].
 *                                      Each CAN unit has 8 receive buffer slots. When the number of received frames \
 *                                      reaches the value specified by parameter 'u8RBSWarnLimit', register bit RTIF.RAFIF is set and \
 *                                      the interrupt occurred if it was enabled.
 * @retval None
 */
void CAN_SetRBSWarnLimit(CANFD_TypeDef xdata* CANx, uint8_t u8RBSWarnLimit)
{
    u8RBSWarnLimit <<= CAN_LIMIT_AFWL_POS;
    CANFD_MODIFY_REG8(CANx->LIMIT, CAN_LIMIT_AFWL, u8RBSWarnLimit);
}

/**
 * @brief  Set error warning limit.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  u8ErrWarnLimit         Programmable error warning limit. Range is [0, 15].
 *                                      Error warning limit = (u8ErrWarnLimit + 1) * 8.
 * @retval None
 */
void CAN_SetErrWarnLimit(CANFD_TypeDef xdata* CANx, uint8_t u8ErrWarnLimit)
{
    CANFD_MODIFY_REG8(CANx->LIMIT, CAN_LIMIT_EWL, u8ErrWarnLimit);
}

/**
 * @brief  Configures the specified CAN FD according to the specified parameters
 *         in the stc_can_fd_cfg_t type structure.
 * @param  [in]  CANx                   Pointer to CAN instance register base.
 *                                      This parameter can be a value of the following:
 *   @arg  CANFD1:                      CAN unit 1 instance register base.
 *   @arg  CANFD2:                      CAN unit 2 instance register base.
 * @param  [in]  pstcCfg                Pointer to a stc_can_fd_cfg_t structure value that
 *                                      contains the configuration information for the CAN FD.
 * @retval An en_result_t enumeration type value.
 *   @arg  Ok:                          No error occurred.
 *   @arg  ErrorInvalidParameter:       pstcCfg == NULL.
 * @note Call this function when CFG_STAT.RESET is 1.
 */
uint8_t CAN_FD_Config(CANFD_TypeDef xdata* CANx, const stc_can_fd_cfg_t *pstcCfg)
{
    uint8_t enRet = 0;

    if (pstcCfg != NULL)
    {
        /* Specifies CAN FD ISO mode. */
        CANFD_MODIFY_REG8(CANx->TCTRL, CAN_TCTRL_FD_ISO, pstcCfg->u8CANFDMode);

        /*
         * Configures fast bit time.
         * Restrictions: u32SEG1 >= u32SEG2 + 1, u32SEG2 >= u32SJW.
         * TQ = u32Prescaler / CANClock.
         * Fast unsigned char time = (u32SEG1 + u32SEG2) * TQ.
         */
        CANFD_WRITE_REG8(CANx->F_SEG1, pstcCfg->stcFBT.u8SEG1);
        CANFD_WRITE_REG8(CANx->F_SEG2, pstcCfg->stcFBT.u8SEG2);
        CANFD_WRITE_REG8(CANx->F_SJW, pstcCfg->stcFBT.u8SJW);
        CANFD_WRITE_REG8(CANx->F_PRESC, pstcCfg->stcFBT.u8Prescaler);

        /* Specifies the secondary sample point. Number of TQ. */
        CANFD_MODIFY_REG8(CANx->TDC, CAN_TDC_SSPOFF, pstcCfg->u8TDCSSP);

        /* Enable or disable TDC. */
        CANFD_MODIFY_REG8(CANx->TDC, CAN_TDC_TDCEN, pstcCfg->u8TDCCmd);
        enRet = 1;
    }

    return enRet;
}

//========================================================================
void CAN_AFConfig(CANFD_TypeDef xdata* CANx, uint16_t u16AFSel, const stc_can_af_cfg_t pstcAFCfg[])
{
    uint8_t u8AFAddr = 0U;

    if ((u16AFSel != 0U) && (pstcAFCfg != NULL))
    {
        while (u16AFSel != 0U)
        {
            if ((u16AFSel & (uint16_t)0x1U) != 0U)      //判断筛选器组是否使能
            {
                CANFD_WRITE_REG8(CANx->ACFCTRL, u8AFAddr);    //设置ACFADR指向筛选器组的ID Code
                CANFD_WRITE_REG16(CANx->ACF[0], (uint16_t)pstcAFCfg[u8AFAddr].u32ID);
                CANFD_WRITE_REG16(CANx->ACF[1], (uint16_t)(pstcAFCfg[u8AFAddr].u32ID>>16));

                CANFD_SET_REG8_BIT(CANx->ACFCTRL, CAN_ACFCTRL_SELMASK);   //设置ACFADR指向筛选器组的ID Mask
                CANFD_WRITE_REG16(CANx->ACF[0], (uint16_t)(pstcAFCfg[u8AFAddr].u32IDMsk | pstcAFCfg[u8AFAddr].u32MskType));
                CANFD_WRITE_REG16(CANx->ACF[1], (uint16_t)((pstcAFCfg[u8AFAddr].u32IDMsk | pstcAFCfg[u8AFAddr].u32MskType)>>16));
            }
            u16AFSel >>= 1U;
            u8AFAddr++;
        }
    }
}

//========================================================================
uint8_t CAN_FD_Init(CANFD_TypeDef xdata* CANx, const stc_can_init_t *pstcInit)
{
    uint8_t enRet = 0;

    if (pstcInit != NULL)
    {
        /* Software reset. */
        CAN_SWReset(CANx);
        /* Defines slow bit time. */
        (void)CAN_SBTConfig(CANx, &pstcInit->stcSBT);
        /* Specifies STB priority mode. STC32G144K246固定FIFO模式 */
        //CAN_SetSTBPrioMode(CANx, pstcInit->u8STBPrioMode);
        /* Configures acceptance filters. */
        (void)CAN_AFConfig(CANx, pstcInit->u16AFSel, pstcInit->pstcAFCfg);
        /* Configures CAN-FD. */
        (void)CAN_FD_Config(CANx, &pstcInit->stcFDCfg);

        /* CAN bus enters normal communication mode. */
        CAN_EnterNormalComm(CANx);
        /* Specifies work mode. */
        CAN_SetWorkMode(CANx, pstcInit->u8WorkMode);
        /* Specifies transmission mode. */
        CAN_SetTransMode(CANx, pstcInit->u8TransMode);

        CAN_SetRBStoreSel(CANx, pstcInit->u8RBStoreSel);
        CAN_SetRBSWarnLimit(CANx, pstcInit->u8RBSWarnLimit);
        CAN_SetErrWarnLimit(CANx, pstcInit->u8ErrWarnLimit);
        /* Specifies the operation when receiving buffer overflow. */
        CAN_SetRBOvfOp(CANx, pstcInit->u8RBOvfOp);
        /* Enable acceptance filters that configured before. */
        CANFD_WRITE_REG16(CANx->ACFEN, pstcInit->u16AFSel);

    #if (TRANS_MODE == PTB_MODE)
        CANFD_CLEAR_REG8_BIT(CANx->TCMD, CAN_BUF_MASK);   //bit7 0:发送BUF选择PTB
    #else
        CANFD_SET_REG8_BIT(CANx->TCMD, CAN_BUF_MASK);     //bit7 1:发送BUF选择STB
    #endif

        /* Enable or disable self-ACK. */
        CANFD_MODIFY_REG8(CANx->RCTRL, CAN_RCTRL_SACK, pstcInit->u8SelfACKCmd);

        /* Clear all status flags. */
        CAN_ClrStatus(CANx, CAN_FLAG_ALL);

        enRet = 1;
    }

    return enRet;
}

//========================================================================
void CANFD_Init(CANFD_TypeDef xdata* CANx)
{
    stc_can_init_t stcInit;

#if(SET_ENDIAN)
    CANEDIN = 1;    //设置CAN数据大小端，0:{[7:0],[15:8]};  1:{[15:8],[7:0]} 
#else
    CANEDIN = 0;    //设置CAN数据大小端，0:{[7:0],[15:8]};  1:{[15:8],[7:0]} 
#endif

    if(CANx == CAN1)
    {
        CANEN = 1;      //CAN1模块使能
        CAN_S1 = 1;     //CAN1 switch to, 00: P0.0 P0.1, 01: P5.0 P5.1, 10: P4.2 P4.5, 11: P7.0 P7.1
        CAN_S0 = 1;
    }
    else
    {
        CAN2EN = 1;     //CAN2模块使能
        CAN2_S1 = 1;    //CAN2 switch to, 00: P0.2 P0.3, 01: P5.2 P5.3, 10: P4.6 P4.7, 11: P7.2 P7.3
        CAN2_S0 = 1;
    }

    /* Initializes CAN. */
    stcInit.u8WorkMode = CAN_MODE_NORMAL; //CAN_MODE_ELB_SILENT;
    stcInit.pstcAFCfg  = astcAFCfg;
    stcInit.u8RBOvfOp  = CAN_RB_OVF_SAVE_NEW;
    stcInit.u16AFSel   = APP_CAN_AF_SEL;

    stcInit.u8TransMode    = CAN_TRANS_PTB_STB_AUTO_RETX;
    stcInit.u8RBSWarnLimit = 6U;
    stcInit.u8ErrWarnLimit = 7U;
    stcInit.u8RBStoreSel   = CAN_RB_STORE_CORRECT_DATA;
    stcInit.u8SelfACKCmd   = CAN_SELF_ACK_DISABLE;

#if 1   //1:仲裁域1Mbps，数据域5Mbps;  0:仲裁域500Kbps，数据域500Kbps
    //传统CAN总线的位速率
    //仲裁域波特率：Fclk/((SEG1+2)+(SEG2+1))/(Prescaler+1)=80M/40/2=1M
    //设定规则：SEG1>=SEG2+1, SEG2>=SJW
    stcInit.stcSBT.u8SEG1 = 30U;
    stcInit.stcSBT.u8SEG2 = 7U;
    stcInit.stcSBT.u8SJW  = 1U;
    stcInit.stcSBT.u8Prescaler = 1U;

    //可变位速率
    //数据域波特率：Fclk/((SEG1+2)+(SEG2+1))/(Prescaler+1)=80M/16/1=5M
    //设定规则：SEG1>=SEG2, SEG2>=SJW
    stcInit.stcFDCfg.stcFBT.u8SEG1 = 11U;
    stcInit.stcFDCfg.stcFBT.u8SEG2 = 2U;
    stcInit.stcFDCfg.stcFBT.u8SJW  = 2U;
    stcInit.stcFDCfg.stcFBT.u8Prescaler = 0U;
    stcInit.stcFDCfg.u8CANFDMode = CAN_FD_MODE_ISO_11898;   //CAN_FD_MODE_BOSCH, CAN_FD_MODE_ISO_11898
    stcInit.stcFDCfg.u8TDCSSP = (stcInit.stcFDCfg.stcFBT.u8SEG1 + 2);   //TDCSSP 建议设置与 (SEG1+2) 相同值
#else
    //传统CAN总线的位速率
    //仲裁域波特率：Fclk/((SEG1+2)+(SEG2+1))/(Prescaler+1)=80M/40/4=500K
    //设定规则：SEG1>=SEG2+1, SEG2>=SJW
    stcInit.stcSBT.u8SEG1 = 30U;
    stcInit.stcSBT.u8SEG2 = 7U;
    stcInit.stcSBT.u8SJW  = 1U;
    stcInit.stcSBT.u8Prescaler = 3U;

    //可变位速率
    //数据域波特率：Fclk/((SEG1+2)+(SEG2+1))/(Prescaler+1)=80M/40/4=500K
    //设定规则：SEG1>=SEG2, SEG2>=SJW
    stcInit.stcFDCfg.stcFBT.u8SEG1 = 30U;
    stcInit.stcFDCfg.stcFBT.u8SEG2 = 7U;
    stcInit.stcFDCfg.stcFBT.u8SJW  = 1U;
    stcInit.stcFDCfg.stcFBT.u8Prescaler = 3U;
    stcInit.stcFDCfg.u8CANFDMode = CAN_FD_MODE_ISO_11898;   //CAN_FD_MODE_BOSCH, CAN_FD_MODE_ISO_11898
    stcInit.stcFDCfg.u8TDCSSP = (stcInit.stcFDCfg.stcFBT.u8SEG1 + 2);   //TDCSSP 建议设置与 (SEG1+2) 相同值
#endif

    CAN_FD_Init(CANx, &stcInit);

    /* Enable the specified interrupts of CAN. */
    CAN_IntCmd(CANx, CAN_INT_ALL, Enable);

    if(CANx == CAN1)
    {
        CANICR |= 0x02;         //CAN中断使能
    }
    else
    {
        CANICR |= 0x20;         //CAN2中断使能
    }
}

//========================================================================
/* G5 xdata-param: not in demo-82 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
void CAN_SetData(CANFD_TypeDef* CANx, stc_can_tx_t* pcanTx)
{
    uint8_t i,u8DataSize;

    if((pcanTx->TxCtrl.tx_ctrl.FDF == 0) && (pcanTx->TxCtrl.tx_ctrl.DLC > CAN_DLC_8))
    {
        pcanTx->TxCtrl.tx_ctrl.DLC = CAN_DLC_8; //CAN2.0模式下设置DLC大于8的，修改DLC为8
    }
    
    CANx->TBUF[0] = ((uint8_t *)&pcanTx->u32ID)[3];  //ID
    CANx->TBUF[1] = ((uint8_t *)&pcanTx->u32ID)[2];
    CANx->TBUF[2] = ((uint8_t *)&pcanTx->u32ID)[1];
    CANx->TBUF[3] = ((uint8_t *)&pcanTx->u32ID)[0];
    CANx->TBUF[4] = pcanTx->TxCtrl.u8Ctrl;          //CTRL
    CANx->TBUF[5] = pcanTx->RESERVED[0];            //RESERVED
    CANx->TBUF[6] = pcanTx->RESERVED[1];            //RESERVED
    CANx->TBUF[7] = pcanTx->RESERVED[2];            //RESERVED
    
    u8DataSize = u8DLC2Size[pcanTx->TxCtrl.tx_ctrl.FDF][pcanTx->TxCtrl.tx_ctrl.DLC];

    for(i=0;i<u8DataSize;i++)
    {
        CANx->TBUF[8 + i] = pcanTx->pu8Data[i];     //Data
    }
    CANFD_SET_REG8_BIT(CANx->TCTRL, CAN_TCTRL_TSNEXT); //指向下一个STB SLOT
}

//========================================================================
/* G5 xdata-param: not in demo-82 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
void CAN_TransData(CANFD_TypeDef* CANx)
{
    uint8_t i,u8DataSize;
    stc_can_tx_t stcTx;

    stcTx.u32ID = 0x01234567;
    stcTx.TxCtrl.u8Ctrl = 0x00U;
    stcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    stcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    stcTx.TxCtrl.tx_ctrl.FDF = 1;          //0:CAN2.0; 1:CANFD
    stcTx.TxCtrl.tx_ctrl.BRS = 1;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    stcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度

    u8DataSize = u8DLC2Size[stcTx.TxCtrl.tx_ctrl.FDF][stcTx.TxCtrl.tx_ctrl.DLC];
    for(i=0;i<u8DataSize;i++)
    {
        stcTx.pu8Data[i] = 0x10+i;         //Data
    }

    CAN_SetData(CANx,&stcTx);               //将需要发送的数据写入发送缓冲区

#if (TRANS_MODE == STB_MODE)    //STB_MODE 可以缓存 4 帧数据

    stcTx.u32ID = 0x01234568;
    stcTx.TxCtrl.u8Ctrl = 0x00U;
    stcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    stcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    stcTx.TxCtrl.tx_ctrl.FDF = 1;          //0:CAN2.0; 1:CANFD
    stcTx.TxCtrl.tx_ctrl.BRS = 1;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    stcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度

    u8DataSize = u8DLC2Size[stcTx.TxCtrl.tx_ctrl.FDF][stcTx.TxCtrl.tx_ctrl.DLC];
    for(i=0;i<u8DataSize;i++)
    {
        stcTx.pu8Data[i] = 0x20+i;         //Data
    }

    CAN_SetData(CANx,&stcTx);               //将需要发送的数据写入发送缓冲区

    //=============================================================================================
    stcTx.u32ID = 0x01234569;
    stcTx.TxCtrl.u8Ctrl = 0x00U;
    stcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    stcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    stcTx.TxCtrl.tx_ctrl.FDF = 1;          //0:CAN2.0; 1:CANFD
    stcTx.TxCtrl.tx_ctrl.BRS = 1;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    stcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度

    u8DataSize = u8DLC2Size[stcTx.TxCtrl.tx_ctrl.FDF][stcTx.TxCtrl.tx_ctrl.DLC];
    for(i=0;i<u8DataSize;i++)
    {
        stcTx.pu8Data[i] = 0x30+i;         //Data
    }

    CAN_SetData(CANx,&stcTx);               //将需要发送的数据写入发送缓冲区

    //=============================================================================================
    stcTx.u32ID = 0x0123456a;
    stcTx.TxCtrl.u8Ctrl = 0x00U;
    stcTx.TxCtrl.tx_ctrl.IDE = 1;          //0:标准帧; 1:扩展帧
    stcTx.TxCtrl.tx_ctrl.RTR = 0;          //0:数据帧; 1:远程帧(仅适用于CAN2.0, CANFD固定为0)
    stcTx.TxCtrl.tx_ctrl.FDF = 1;          //0:CAN2.0; 1:CANFD
    stcTx.TxCtrl.tx_ctrl.BRS = 1;          //0:整帧为低速波特率; 1:数据和CRC为快速波特率(仅适用于CANFD)
    stcTx.TxCtrl.tx_ctrl.DLC = CAN_DLC_8;  //DLC:数据长度

    u8DataSize = u8DLC2Size[stcTx.TxCtrl.tx_ctrl.FDF][stcTx.TxCtrl.tx_ctrl.DLC];
    for(i=0;i<u8DataSize;i++)
    {
        stcTx.pu8Data[i] = 0x40+i;         //Data
    }

    CAN_SetData(CANx,&stcTx);               //将需要发送的数据写入发送缓冲区

#endif

//    if(CANx == CAN1)
//    {
//        printf("CAN1 Send.\r\n");
//    }
//    else
//    {
//        printf("CAN2 Send.\r\n");
//    }
    
#if (TRANS_MODE == PTB_MODE)
    CANFD_SET_REG8_BIT(CANx->TCMD, CAN_TCMD_TPE);      //bit4 TPE 1:使能PTB发送
#else
    CANFD_SET_REG8_BIT(CANx->TCMD, CAN_TCMD_TSALL);    //bit1 TSALL 1:使能STB发送
#endif
}

//========================================================================
/* G5 xdata-param: not in demo-82 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
void CAN_SendData(CANFD_TypeDef* CANx, stc_can_tx_t* pcanTx)
{
    uint8_t i,u8DataSize;

    if((pcanTx->TxCtrl.tx_ctrl.FDF == 0) && (pcanTx->TxCtrl.tx_ctrl.DLC > CAN_DLC_8))
    {
        pcanTx->TxCtrl.tx_ctrl.DLC = CAN_DLC_8; //CAN2.0模式下设置DLC大于8的，修改DLC为8
    }
    
    CANx->TBUF[0] = ((uint8_t *)&pcanTx->u32ID)[3];  //ID
    CANx->TBUF[1] = ((uint8_t *)&pcanTx->u32ID)[2];
    CANx->TBUF[2] = ((uint8_t *)&pcanTx->u32ID)[1];
    CANx->TBUF[3] = ((uint8_t *)&pcanTx->u32ID)[0];
    CANx->TBUF[4] = pcanTx->TxCtrl.u8Ctrl;          //CTRL
    CANx->TBUF[5] = pcanTx->RESERVED[0];            //RESERVED
    CANx->TBUF[6] = pcanTx->RESERVED[1];            //RESERVED
    CANx->TBUF[7] = pcanTx->RESERVED[2];            //RESERVED
    
    u8DataSize = u8DLC2Size[pcanTx->TxCtrl.tx_ctrl.FDF][pcanTx->TxCtrl.tx_ctrl.DLC];

    for(i=0;i<u8DataSize;i++)
    {
        CANx->TBUF[8 + i] = pcanTx->pu8Data[i];     //Data
    }

    CANFD_SET_REG8_BIT(CANx->TCTRL, CAN_TCTRL_TSNEXT); //指向下一个STB SLOT
#if (TRANS_MODE == PTB_MODE)
    CANFD_SET_REG8_BIT(CANx->TCMD, CAN_TCMD_TPE);      //bit4 TPE 1:使能PTB发送
#else
    CANFD_SET_REG8_BIT(CANx->TCMD, CAN_TCMD_TSALL);    //bit1 TSALL 1:使能STB发送
#endif
}

//========================================================================
/* G5 xdata-param: not in demo-82 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
uint8_t CAN_ReceiveData(CANFD_TypeDef* CANx, stc_can_rx_t* pstcRx)
{
    uint32_t i;
    uint8_t u8DataSize;
    uint8_t u8RxFrameCnt = 0U;

    while (CANFD_READ_REG8_BIT(CANx->RCTRL, CAN_RCTRL_RSTAT) != CAN_RB_STAT_EMPTY)
    {
        CANFD_READ_CAN_ID(pstcRx[u8RxFrameCnt].u32ID);
        CANFD_READ_CAN_CTRL(pstcRx[u8RxFrameCnt].RxCtrl.u16Ctrl);

        u8DataSize = u8DLC2Size[pstcRx[u8RxFrameCnt].RxCtrl.rx_ctrl.FDF][pstcRx[u8RxFrameCnt].RxCtrl.rx_ctrl.DLC];

        i = 0U;
        while (i < u8DataSize)
        {
            pstcRx[u8RxFrameCnt].pu8Data[i] = CANFD_READ_REG8(CANx->RBUF[i+8]);
            i++;
        }

        /* Set RB to point to the next RB slot. */
        CANFD_SET_REG8_BIT(CANx->RCTRL, CAN_RCTRL_RREL);
        u8RxFrameCnt++;
        if (u8RxFrameCnt >= 8)
        {
            break;
        }
    }

    return u8RxFrameCnt;
}

//========================================================================
// 函数: void CAN1_Interrupt(void) interrupt CAN_VECTOR
// 描述: CAN总线中断函数。
// 参数: none.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-9-19
// 备注: 
//========================================================================
void __attribute__((interrupt(28))) CAN1_Interrupt(void)
{
    uint8_t isr;

    isr = CANFD_READ_REG8(CAN1->RTIF);
    if((isr & 0x80) == 0x80)  //RIF-接收中断标志
    {
        B_CanRead = 1;
    }
    if((isr & 0x08) == 0x08)  //TPIF-PTB发送中断标志
    {
        B_CanSend = 0;
    }
    if((isr & 0x04) == 0x04)  //TSIF-STB接收中断标志
    {
        B_CanSend = 0;
    }

    CANFD_WRITE_REG8(CAN1->RTIF, isr);    //写1清除中断标志

    //================================================
    isr = CANFD_READ_REG8(CAN1->RCTRL) & 0x20;    //ROV
    if(isr == 0x20)  //ROV-接收BUF溢出标志
    {
    }

    CANFD_SET_REG8_BIT(CAN1->RCTRL, isr); //写1清除中断标志

    //================================================
    isr = CANFD_READ_REG8(CAN1->ERRINT) & 0x15;
    if((isr & 0x01) == 0x01)  //BEIF-总线错误标志
    {
    }
    if((isr & 0x04) == 0x04)  //ALIF-仲裁失败标志
    {
    }
    if((isr & 0x10) == 0x10)  //EPIF-错误被动中断标志
    {
    }

    CANFD_SET_REG8_BIT(CAN1->ERRINT, isr);//写1清除中断标志
}

//========================================================================
// 函数: void CAN2_Interrupt(void) interrupt CAN_VECTOR
// 描述: CAN2总线中断函数。
// 参数: none.
// 返回: none.
// 版本: VER1.0
// 日期: 2025-9-19
// 备注: 
//========================================================================
void __attribute__((interrupt(29))) CAN2_Interrupt(void)
{
    uint8_t isr;

    isr = CANFD_READ_REG8(CAN2->RTIF);
    if((isr & 0x80) == 0x80)  //RIF-接收中断标志
    {
        B_Can2Read = 1;
    }
    if((isr & 0x08) == 0x08)  //TPIF-PTB发送中断标志
    {
        B_Can2Send = 0;
    }
    if((isr & 0x04) == 0x04)  //TSIF-STB接收中断标志
    {
        B_Can2Send = 0;
    }

    CANFD_WRITE_REG8(CAN2->RTIF, isr);    //写1清除中断标志

    //================================================
    isr = CANFD_READ_REG8(CAN2->RCTRL) & 0x20;    //ROV
    if(isr == 0x20)  //ROV-接收BUF溢出标志
    {
    }

    CANFD_SET_REG8_BIT(CAN2->RCTRL, isr); //写1清除中断标志

    //================================================
    isr = CANFD_READ_REG8(CAN2->ERRINT) & 0x15;
    if((isr & 0x01) == 0x01)  //BEIF-总线错误标志
    {
    }
    if((isr & 0x04) == 0x04)  //ALIF-仲裁失败标志
    {
    }
    if((isr & 0x10) == 0x10)  //EPIF-错误被动中断标志
    {
    }

    CANFD_SET_REG8_BIT(CAN2->ERRINT, isr);//写1清除中断标志
}
