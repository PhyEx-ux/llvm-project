/*---------------------------------------------------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/* --- BBS: www.STCAIMCU.com  -----------------------------------------*/
/*---------------------------------------------------------------------*/

#ifndef _CAN_FD_H
#define _CAN_FD_H














#define PTB_MODE        0
#define STB_MODE        1
#define TRANS_MODE      PTB_MODE

#define SET_ENDIAN      0       //设置16位数据的大小端，0:{[7:0],[15:8]};  1:{[15:8],[7:0]}

/* Acceptance filter. */
#define APP_CAN_AF_SEL  (CAN_AF1 | CAN_AF2 | CAN_AF3 | CAN_AF10)

#define APP_CAN_AF1_ID                      (0xA1U)
#define APP_CAN_AF1_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 0001'. */
#define APP_CAN_AF1_MSK_TYPE                (CAN_AF_MSK_EXT)        /* Only accept standard ID. */

#define APP_CAN_AF2_ID                      (0xB2U)
#define APP_CAN_AF2_ID_MSK                  (0xB0U)                 /* Accept messages with ID 'x0xx 0010'. */
#define APP_CAN_AF2_MSK_TYPE                (CAN_AF_MSK_EXT)        /* Accept extended ID. */

#define APP_CAN_AF3_ID                      (0xC3U)
//#define APP_CAN_AF3_ID_MSK                  (0x03U)                 /* Accept messages with ID '1100 00xx'. */
#define APP_CAN_AF3_ID_MSK                  (CAN_ACF_ACODEORAMASK)  /* Accept messages with any ID. */
#define APP_CAN_AF3_MSK_TYPE                (CAN_AF_MSK_STD_EXT)    /* Accept standard ID and extended ID. */

#define APP_CAN_AF4_ID                      (0xA4U)
#define APP_CAN_AF4_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 0100'. */
#define APP_CAN_AF4_MSK_TYPE                (CAN_AF_MSK_STD_EXT)    /* Only accept standard ID. */

#define APP_CAN_AF5_ID                      (0xA5U)
#define APP_CAN_AF5_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 0101'. */
#define APP_CAN_AF5_MSK_TYPE                (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF6_ID                      (0xA6U)
#define APP_CAN_AF6_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 0110'. */
#define APP_CAN_AF6_MSK_TYPE                (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF7_ID                      (0xA7U)
#define APP_CAN_AF7_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 0111'. */
#define APP_CAN_AF7_MSK_TYPE                (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF8_ID                      (0xA8U)
#define APP_CAN_AF8_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 1000'. */
#define APP_CAN_AF8_MSK_TYPE                (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF9_ID                      (0xA9U)
#define APP_CAN_AF9_ID_MSK                  (0x00U)                 /* Only accept messages with ID '1010 1001'. */
#define APP_CAN_AF9_MSK_TYPE                (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF10_ID                     (0xAAU)
#define APP_CAN_AF10_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1010'. */
#define APP_CAN_AF10_MSK_TYPE               (CAN_AF_MSK_STD_EXT)    /* Only accept standard ID. */

#define APP_CAN_AF11_ID                     (0xABU)
#define APP_CAN_AF11_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1011'. */
#define APP_CAN_AF11_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF12_ID                     (0xACU)
#define APP_CAN_AF12_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1100'. */
#define APP_CAN_AF12_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF13_ID                     (0xADU)
#define APP_CAN_AF13_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1101'. */
#define APP_CAN_AF13_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF14_ID                     (0xAEU)
#define APP_CAN_AF14_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1110'. */
#define APP_CAN_AF14_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF15_ID                     (0xAFU)
#define APP_CAN_AF15_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1010 1111'. */
#define APP_CAN_AF15_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */

#define APP_CAN_AF16_ID                     (0xB1U)
#define APP_CAN_AF16_ID_MSK                 (0x00U)                 /* Only accept messages with ID '1011 0001'. */
#define APP_CAN_AF16_MSK_TYPE               (CAN_AF_MSK_STD)        /* Only accept standard ID. */


/**
 * @brief CANFD register structure.
 */
typedef struct CANFD_struct
{
    volatile unsigned char RBUF[80];
    volatile unsigned char TBUF[72];
    volatile unsigned char RESERVED0[8];
    volatile unsigned char CFG_STAT;
    volatile unsigned char TCMD;
    volatile unsigned char TCTRL;
    volatile unsigned char RCTRL;
    volatile unsigned char RTIE;
    volatile unsigned char RTIF;
    volatile unsigned char ERRINT;
    volatile unsigned char LIMIT;
    volatile unsigned char S_SEG1;
    volatile unsigned char S_SEG2;
    volatile unsigned char S_SJW;
    volatile unsigned char S_PRESC;
    volatile unsigned char F_SEG1;
    volatile unsigned char F_SEG2;
    volatile unsigned char F_SJW;
    volatile unsigned char F_PRESC;
    volatile unsigned char EALCAP;
    volatile unsigned char TDC;
    volatile unsigned char RECNT;
    volatile unsigned char TECNT;
    volatile unsigned char ACFCTRL;
    volatile unsigned char RESERVED1;
    volatile unsigned int  ACFEN;
    volatile unsigned int  ACF[2];
    volatile unsigned char RESERVED2[2];
    volatile unsigned char TBSLOT;
    volatile unsigned char TTCFG;
    volatile unsigned char REF_MSG[4];
    volatile unsigned int  TRG_CFG;
    volatile unsigned int  TT_TRIG;
    volatile unsigned int  TT_WTRIG;
}CANFD_TypeDef;

#define CAN1_BaseAddress    0x7ef400
#define CAN2_BaseAddress    0x7ef300

#define CAN1                ((CANFD_TypeDef __xdata*)CAN1_BaseAddress)
#define CAN2                ((CANFD_TypeDef __xdata*)CAN2_BaseAddress)


#define CANFD_SET_REG8_BIT(REG, BIT)          ((ACC = REG),(REG) |= ((uint8_t)(BIT)))
#define CANFD_CLEAR_REG8_BIT(REG, BIT)        ((ACC = REG),(REG) &= ((uint8_t)(~((uint8_t)(BIT)))))
#define CANFD_READ_REG8_BIT(REG, BIT)         ((ACC = (REG) & ((uint8_t)(BIT))),((REG) & ((uint8_t)(BIT))))
#define CANFD_READ_REG8(REG)                  ((ACC = REG),(REG))

#define CANFD_CLEAR_REG8(REG)                 ((REG) = ((uint8_t)(0U)))
#define CANFD_CLEAR_REG16(REG)                ((REG) = ((uint16_t)(0U)))
#define CANFD_WRITE_REG8(REG, VAL)            ((REG) = ((uint8_t)(VAL)))

#define CANFD_MODIFY_REG8(REGS, CLEARMASK, SETMASK)   (CANFD_WRITE_REG8((REGS), (((CANFD_READ_REG8((REGS))) & ((uint8_t)(~((uint8_t)(CLEARMASK))))) | ((uint8_t)(SETMASK) & (uint8_t)(CLEARMASK)))))

#define CANFD_READ_REG(REG, VAL) \
    do { \
        (VAL) = (REG); \
        (VAL) = (REG); \
    } while (0)

#define CANFD_READ_CAN_ID(VAL) \
    do { \
        (VAL) = (((uint32_t)CANx->RBUF[3]<<24 | (uint32_t)CANx->RBUF[2]<<16 | (uint32_t)CANx->RBUF[1]<<8 | (uint32_t)CANx->RBUF[0]) & 0x1FFFFFFFUL); \
        (VAL) = (((uint32_t)CANx->RBUF[3]<<24 | (uint32_t)CANx->RBUF[2]<<16 | (uint32_t)CANx->RBUF[1]<<8 | (uint32_t)CANx->RBUF[0]) & 0x1FFFFFFFUL); \
    } while (0)

#define CANFD_READ_CAN_CTRL(VAL) \
    do { \
        (VAL) = ((uint16_t)(CANx->RBUF[4]<<8) | (CANx->RBUF[5])); \
        (VAL) = ((uint16_t)(CANx->RBUF[4]<<8) | (CANx->RBUF[5])); \
    } while (0)

#if(SET_ENDIAN)

#define CANFD_READ_REG16(REG)                 ((ACC = REG),(REG))
#define CANFD_SET_REG16_BIT(REG, BIT)         ((ACC = REG),(REG) |= ((uint16_t)(BIT)))
#define CANFD_CLEAR_REG16_BIT(REG, BIT)       ((ACC = REG),(REG) &= ((uint16_t)(~((uint16_t)(BIT)))))
#define CANFD_READ_REG16_BIT(REG, BIT)        ((ACC = (REG) & ((uint16_t)(BIT))),((REG) & ((uint16_t)(BIT))))
#define CANFD_WRITE_REG16(REG, VAL)           ((REG) = ((uint16_t)(VAL)))
#define CANFD_WRITE_BUF16(REG, VAL)           ((REG) = ((uint16_t)(VAL)))

#else
    
extern uint16_t reverse2(uint16_t w);

#define CANFD_READ_REG16(REG)                 ((ACC = reverse2(REG)),(reverse2(REG)))
#define CANFD_SET_REG16_BIT(REG, BIT)         ((ACC = REG),(REG) |= ((uint16_t)reverse2(BIT)))
#define CANFD_CLEAR_REG16_BIT(REG, BIT)       ((ACC = REG),(REG) &= ((uint16_t)(~((uint16_t)reverse2(BIT)))))
#define CANFD_READ_REG16_BIT(REG, BIT)        ((ACC = (REG) & ((uint16_t)reverse2(BIT))),((REG) & ((uint16_t)reverse2(BIT))))
#define CANFD_WRITE_REG16(REG, VAL)           ((REG) = ((uint16_t)reverse2(VAL)))
#define CANFD_WRITE_BUF16(REG, VAL)           ((REG) = ((uint16_t)reverse2(VAL)))

#endif

/**
 * @brief Functional state
 */
typedef enum
{
    Disable = 0U,
    Enable  = 1U,
} en_functional_state_t;

/**
 * @brief CAN bit timing configuration structure. See 11898-1 for details.
 * @note 1. Restrictions: u32SEG1 >= u32SEG2 + 1, u32SEG2 >= u32SJW.
 * @note 2. TQ = u32Prescaler / CANClock.
 * @note 3. Bit time = (u32SEG2 + u32SEG2) x TQ.
 * @note 4. Baudrate = CANClock/(u32Prescaler*(u32SEG1 + u32SEG2))
 */
typedef struct
{
    uint8_t u8SEG1;                         /*!< TQs of segment 1. Contains synchronization segment, \
                                                 propagation time segment and phase buffer segment 1. */
    uint8_t u8SEG2;                         /*!< TQs of segment 2. Phase buffer segment 2. */
    uint8_t u8SJW;                          /*!< TQs of synchronization jump width. */
    uint8_t u8Prescaler;                    /*!< Range [1, 256]. */
} stc_can_bt_cfg_t;

/**
 * @brief CAN acceptance filter structure.
 */
typedef struct
{
    uint32_t u32ID;                         /*!< 11 bits standard ID or 29 bits extended ID, depending on IDE. */
    uint32_t u32IDMsk;                      /*!< ID mask. The mask bits of ID will be ignored by the acceptance filter. */
    uint32_t u32MskType;                    /*!< Acceptance filter mask type. This parameter can be a value of @ref CAN_AF_Mask_Type */
} stc_can_af_cfg_t;

/**
 * @brief CANFD configuration structure.
 */
typedef struct
{
    uint8_t u8CANFDMode;                    /*!< CAN-FD mode, Bosch CAN-FD or 11898-1:2015 CAN-FD.
                                                 This parameter can be a value of @ref CAN_FD_Mode */
    stc_can_bt_cfg_t stcFBT;                /*!< Bit timing configuration of fast bit timing. */
    uint8_t u8TDCCmd;                       /*!< Transmiter delay compensation function control.
                                                 This parameter can be a value of @ref CAN_FD_TDC_Command */
    uint8_t u8TDCSSP;                       /*!< Specify secondary sample point(SSP) of transmitter delay compensatin(TDC). Number of TQ. */
} stc_can_fd_cfg_t;

/**
 * @brief CAN initialization structure.
 */
typedef struct
{
    uint8_t u8WorkMode;                     /*!< Specify the work mode of CAN.
                                                 This parameter can be a value of @ref CAN_Work_Mode */
    stc_can_bt_cfg_t stcSBT;                /*!< Bit timing configuration of slow bit timing. */
    stc_can_fd_cfg_t stcFDCfg;              /*!< CAN-FD configuration structure. */
    uint8_t u8TransMode;                    /*!< Transmission mode of PTB and STB.
                                                 This parameter can be a value of @ref CAN_Trans_Mode */
    uint8_t u8STBPrioMode;                  /*!< Priority mode of STB. First in first transmit. OR the priority is determined by ID. Smaller ID higher priority.
                                                 Whatever the priority mode of STB is, PTB always has the highest priority.
                                                 This parameter can be a value of @ref CAN_STB_Priority_Mode */
    uint8_t u8RBSWarnLimit;                 /*!< Specify receive buffer almost full warning limit. Rang is [1, 8]. \
                                                 Each CAN unit has 8 receive buffer slots. When the number of received frames reaches \
                                                 the set value of u8RBSWarnLimit, register bit RTIF.RAFIF is set and the interrupt occurred \
                                                 if it was enabled. */
    uint8_t u8ErrWarnLimit;                 /*!< Specify programmable error warning limit. Range is [0, 15]. \
                                                 Error warning limit = (u8ErrWarnLimit + 1) * 8. */
    stc_can_af_cfg_t *pstcAFCfg;            /*!< Points to a stc_can_af_cfg_t structure type pointer value that
                                                 contains the configuration informations for the acceptance filters. */
    uint16_t u16AFSel;                      /*!< Specify acceptance filter for receive buffer.
                                                 This parameter can be values of @ref CAN_AF */
    uint8_t u8RBStoreSel;                   /*!< Receive buffer stores all frames, includes error .
                                                 This parameter can be a value of @ref CAN_RB_Store_Selection */
    uint8_t u8RBOvfOp;                      /*!< Operation when receive buffer overflow.
                                                 This parameter can be a value of @ref CAN_RB_Overflow_Operation */
    uint8_t u8SelfACKCmd;                   /*!< Self ACK. Only for external loopback mode.
                                                 This parameter can be a value of @ref CAN_Self_ACK_Command */
} stc_can_init_t;

/**
 * @brief CAN transmit ctrl structure.
 */
union can_tx_ctrl
{
    uint8_t u8Ctrl;
    struct
    {
        uint8_t DLC: 4;                /*!< Data length code. Length of the segment of frame. \
                                             It should be zero while the frame is remote frame. \
                                             This parameter can be a value of @defgroup CAN_DLC */
        uint8_t BRS: 1;                /*!< Bit rate switch. */
        uint8_t FDF: 1;                /*!< CANFD frame. */
        uint8_t RTR: 1;                /*!< Remote transmission request bit.
                                             It is used to distinguish between frames and remote frames. */
        uint8_t IDE: 1;                /*!< Identifier extension flag.
                                             It is used to distinguish between standard format and extended format.
                                             This parameter can be a 1 or 0. */
    }tx_ctrl;
};

/**
 * @}
 */

/**
 * @brief CAN receive structure.
 */
union can_rx_ctrl
{
    uint16_t u16Ctrl;
    struct
    {
        uint8_t DLC: 4;                /*!< Data length code. Length of the segment of frame. \
                                             It should be zero while the frame is remote frame. \
                                             This parameter can be a value of @defgroup CAN_DLC */
        uint8_t BRS: 1;                /*!< Bit rate switch. */
        uint8_t FDF: 1;                /*!< CANFD frame. */
        uint8_t RTR: 1;                /*!< Remote transmission request bit.
                                             It is used to distinguish between frames and remote frames. */
        uint8_t IDE: 1;                /*!< Identifier extension flag.
                                             It is used to distinguish between standard format and extended format.
                                             This parameter can be a 1 or 0. */
        uint8_t RSVD: 4;               /*!< Reserved bits. */
        uint8_t TX: 1;                 /*!< This bit is set to 1 when receiving self-transmitted in loopback mode. */
        uint8_t ERRT: 3;               /*!< Error type. */
    }rx_ctrl;
};

typedef struct
{
    uint32_t u32ID;                    /*!< 11 bits standard ID or 29 bits extended ID, depending on IDE. */
    union    can_rx_ctrl RxCtrl;
    uint16_t CYCLE_TIME;               /*!< Cycle time of time-triggered communication(TTC). */
    uint8_t  pu8Data[64];              /*!< Pointer to filed of frame. */
}stc_can_rx_t;

typedef struct
{
    uint32_t u32ID;                    /*!< 11 bits standard ID or 29 bits extended ID, depending on IDE. */
    union    can_tx_ctrl TxCtrl;
    uint8_t  RESERVED[3];              /*!< Reserved */
    uint8_t  pu8Data[64];              /*!< Pointer to filed of frame. */
}stc_can_tx_t;

/**
 * @}
 */

/**
 * @brief CAN Command structure.
 */
union can_tcmd
{
    uint8_t u8TCMD;
    struct
    {
        uint8_t TSA: 1;                /*!< Transmit Secondary Abort. */
        uint8_t TSALL: 1;              /*!< Transmit Secondary ALL frame. */
        uint8_t TSONE: 1;              /*!< Transmit Secondary ONE frame. */
        uint8_t TPA: 1;                /*!< Transmit Primary Abort. */
        uint8_t TPE: 1;                /*!< Transmit Primary Enable. */
        uint8_t RSVD: 1;               /*!< Reserved bits. */
        uint8_t LOM: 1;                /*!< Listen Only Mode. */
        uint8_t TBSEL: 1;              /*!< Transmit Buffer Select.
                                             This parameter can be a value of @defgroup CAN_Transmit_Buffer_Type */
    }tcmd;
};

/**
 * @}
 */

/**
 * @brief CAN Transmit Control structure.
 */
union can_tctrl
{
    uint8_t u8TCTRL;
    struct
    {
        uint8_t TSSTAT: 2;             /*!< Transmission Secondary Status bits. */
        uint8_t RSVD: 2;               /*!< Reserved bits. */
        uint8_t TTTBM: 1;              /*!< TTCAN Transmit Buffer Mode. */
        uint8_t TSMODE: 1;             /*!< Transmit buffer Secondary operation MODE.
                                             This parameter can be a value of @defgroup CAN_STB_Priority_Mode */
        uint8_t TSNEXT: 1;             /*!< Transmit buffer Secondary NEXT. */
        uint8_t FD_ISO: 1;             /*!< CAN-FD mode, Bosch CAN-FD or 11898-1:2015 CAN-FD.
                                             This parameter can be a value of @defgroup CAN_FD_Mode */
    }tctrl;
};

/**
 * @}
 */

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
/**
 * @defgroup CAN_Global_Macros CAN Global Macros
 * @{
 */

/**
 * @defgroup CAN_Work_Mode CAN Work Mode
 * @{
 */
#define CAN_MODE_NORMAL                 (0U)                    /*!< Normal work mode. */
#define CAN_MODE_SILENT                 (1U)                    /*!< Silent work mode. Prohibit transmission. */
#define CAN_MODE_ILB                    (2U)                    /*!< Internal loopback mode, just for self-test while developing. */
#define CAN_MODE_ELB                    (3U)                    /*!< External loopback mode, just for self-test while developing. */
#define CAN_MODE_ELB_SILENT             (4U)                    /*!< External loppback silent mode, just for self-test while developing. \
                                                                     It is forbidden to respond to received frames and error frames, but can be transmitted. */
/**
 * @}
 */

/**
 * @defgroup CAN_Transmit_Buffer_Type CAN Transmit Buffer Type
 * @{
 */
#define CAN_BUF_PTB                     (0U)                    /*!< Primary transmit buffer. */
#define CAN_BUF_STB                     (1U)                    /*!< Secondary transmit buffer. */
#define CAN_BUF_MASK                    (0x80U)
/**
 * @}
 */

/**
 * @defgroup CAN_STB_Priority_Mode CAN STB Priority Mode
 * @{
 */
#define CAN_STB_PRIO_FIFO               (0x00U)                /*!< Data first in and first be transmitted. */
#define CAN_STB_PRIO_ID                 (0x20U)                /*!< Data with smallest ID first be transmitted. */
/**
 * @}
 */


/**
 * @defgroup CAN_FD_Mode CANFD Mode
 * @{
 */
#define CAN_FD_MODE_BOSCH               (0x00U)
#define CAN_FD_MODE_ISO_11898           (0x80U)
/**
 * @}
 */

/**
 * @defgroup CAN_RB_Store_Selection CAN Receive Buffer Store Selection
 * @{
 */
#define CAN_RB_STORE_CORRECT_DATA       (0x0U)                  /*!< Receive buffer stores correct frames only. */
#define CAN_RB_STORE_ALL_DATA           (0x08U)                 /*!< Receive buffer stores all frames, includes error . */
/**
 * @}
 */

/**
 * @defgroup CAN_RB_Overflow_Operation CAN Receive Buffer Overflow Operation
 * @{
 */
#define CAN_RB_OVF_SAVE_NEW             (0x0U)                  /*!< Saves the newly received and the first received will be overwritten. */
#define CAN_RB_OVF_DISCARD_NEW          (0x40U)                 /*!< Discard the newly received . */
/**
 * @}
 */

/**
 * @defgroup CAN_Self_ACK_Command CAN Self ACK Command
 * @{
 */
#define CAN_SELF_ACK_DISABLE            (0x0U)
#define CAN_SELF_ACK_ENABLE             (0x80U)
/**
 * @}
 */

/**
 * @defgroup CAN_FD_TDC_Command CAN-FD Transmiter Delay Compensation Command
 * @{
 */
#define CAN_FD_TDC_DISABLE              (0x0U)
#define CAN_FD_TDC_ENABLE               (0x80U)
/**
 * @}
 */

/**
 * @defgroup CAN_Interrupt_Type CAN Interrupt Type
 * @{
 */
#define CAN_INT_ERR_INT                 (1UL << 1U)             /*!< Register bit RTIE.EIE. Error interrupt. */
#define CAN_INT_STB_TRANS_OK            (1UL << 2U)             /*!< Register bit RTIE.TSIE. Secondary transmit buffer was transmitted successfully. */
#define CAN_INT_PTB_TRANS_OK            (1UL << 3U)             /*!< Register bit RTIE.TPIE. Primary transmit buffer was transmitted successfully. */
#define CAN_INT_RB_ALMOST_FULL          (1UL << 4U)             /*!< Register bit RTIE.RAFIE. The number of filled RB slot is greater than or equal to the LIMIT.AFWL setting value. */
#define CAN_INT_RB_FIFO_FULL            (1UL << 5U)             /*!< Register bit RTIE.RFIE. The FIFO of receive buffer is full. */
#define CAN_INT_RX_OVERRUN              (1UL << 6U)             /*!< Register bit RTIE.ROIE. Receive buffers are full and there is a further message to be stored. */
#define CAN_INT_RX                      (1UL << 7U)             /*!< Register bit RTIE.RIE. Received a valid frame or remote frame. */
#define CAN_INT_BUS_ERR                 (1UL << 9U)             /*!< Register bit ERRINT.BEIE. Arbitration lost caused bus error */
#define CAN_INT_ARB_LOST                (1UL << 11U)            /*!< Register bit ERRINT.ALIE. Arbitration lost. */
#define CAN_INT_ERR_PASSIVE             (1UL << 13U)            /*!< Register bit ERRINT.EPIE. A change from error-passive to error-active or error-active to error-passive has occurred. */

#define CAN_INT_ALL                     (CAN_INT_ERR_INT        | \
                                         CAN_INT_STB_TRANS_OK   | \
                                         CAN_INT_PTB_TRANS_OK   | \
                                         CAN_INT_RB_ALMOST_FULL | \
                                         CAN_INT_RB_FIFO_FULL   | \
                                         CAN_INT_RX_OVERRUN     | \
                                         CAN_INT_RX             | \
                                         CAN_INT_BUS_ERR        | \
                                         CAN_INT_ARB_LOST       | \
                                         CAN_INT_ERR_PASSIVE)
/**
 * @}
 */

/**
 * @defgroup CAN_Common_Status_Flag CAN Common Status Flag
 * @{
 */
#define CAN_FLAG_BUS_OFF                (1UL << 0U)             /*!< Register bit CFG_STAT.BUSOFF. CAN bus off. */
#define CAN_FLAG_BUS_TX                 (1UL << 1U)             /*!< Register bit CFG_STAT.TACTIVE. CAN bus is transmitting. */
#define CAN_FLAG_BUS_RX                 (1UL << 2U)             /*!< Register bit CFG_STAT.RACTIVE. CAN bus is receiving. */
#define CAN_FLAG_CFG_STAT_MSK           (7UL << 0U)             /*!< Register bit CFG_STAT.BUSOFF/TACTIVE/RACTIVE. */
#define CAN_FLAG_RB_OVF                 (1UL << 5U)             /*!< Register bit RCTRL.ROV. Receive buffer is full and there is a further bit to be stored. At least one is lost. */
#define CAN_FLAG_TB_FULL                (1UL << 8U)             /*!< Register bit RTIE.TSFF. Transmit buffers are all full. \
                                                                     TTCFG.TTEN == 0 or TCTRL.TTTEM == 0: ALL STB slots are filled. \
                                                                     TTCFG.TTEN == 1 and TCTRL.TTTEM == 1: Transmit buffer that pointed by TBSLOT.TBPTR is filled.*/
#define CAN_FLAG_TRANS_ABORTED          (1UL << 16U)            /*!< Register bit RTIF.AIF. Transmit messages requested via TCMD.TPA and TCMD.TSA were successfully canceled. */
#define CAN_FLAG_ERR_INT                (1UL << 17U)            /*!< Register bit RTIF.EIF. The CFG_STAT.BUSOFF unsigned char changes, or the relative relationship between the value of the error counter and the \
                                                                     set value of the ERROR warning limit changes. For example, the value of the error counter changes from less than \
                                                                     the set value to greater than the set value, or from greater than the set value to less than the set value. */
#define CAN_FLAG_STB_TRANS_OK           (1UL << 18U)            /*!< Register bit RTIF.TSIF. STB was transmitted successfully. */
#define CAN_FLAG_PTB_TRANS_OK           (1UL << 19U)            /*!< Register bit RTIF.TPIF. PTB was transmitted successfully. */
#define CAN_FLAG_RB_ALMOST_FULL         (1UL << 20U)            /*!< Register bit RTIF.RAFIF. The number of filled RB slot is greater than or equal to the LIMIT.AFWL setting value. */
#define CAN_FLAG_RB_FIFO_FULL           (1UL << 21U)            /*!< Register bit RTIF.RFIF. The FIFO of receive buffer is full. */
#define CAN_FLAG_RX_OVERRUN             (1UL << 22U)            /*!< Register bit RTIF.ROIF. Receive buffers are all full and there is a further message to be stored. */
#define CAN_FLAG_RX_OK                  (1UL << 23U)            /*!< Register bit RTIF.RIF. Received a valid frame or remote frame. */
#define CAN_FLAG_BUS_ERR                (1UL << 24U)            /*!< Register bit ERRINT.BEIF. Arbitration lost caused bus error. */
#define CAN_FLAG_ARB_LOST               (1UL << 26U)            /*!< Register bit ERRINT.ALIF. Arbitration lost. */
#define CAN_FLAG_ERR_PASSIVE            (1UL << 28U)            /*!< Register bit ERRINT.EPIF. A change from error-passive to error-active or error-active to error-passive has occurred. */
#define CAN_FLAG_ERR_PASSIVE_NODE       (1UL << 30U)            /*!< Register bit ERRINT.EPASS. The node is an error-passive node. */
#define CAN_FLAG_REACH_WARN_LIMIT       (1UL << 31U)            /*!< Register bit ERRINT.EWARN. REC or TEC is greater than or equal to the LIMIT.EWL setting value. */

#define CAN_FLAG_ALL                    (CAN_FLAG_BUS_OFF          | \
                                         CAN_FLAG_BUS_TX           | \
                                         CAN_FLAG_BUS_RX           | \
                                         CAN_FLAG_RB_OVF           | \
                                         CAN_FLAG_TB_FULL          | \
                                         CAN_FLAG_TRANS_ABORTED    | \
                                         CAN_FLAG_ERR_INT          | \
                                         CAN_FLAG_STB_TRANS_OK     | \
                                         CAN_FLAG_PTB_TRANS_OK     | \
                                         CAN_FLAG_RB_ALMOST_FULL   | \
                                         CAN_FLAG_RB_FIFO_FULL     | \
                                         CAN_FLAG_RX_OVERRUN       | \
                                         CAN_FLAG_RX_OK            | \
                                         CAN_FLAG_BUS_ERR          | \
                                         CAN_FLAG_ARB_LOST         | \
                                         CAN_FLAG_ERR_PASSIVE      | \
                                         CAN_FLAG_ERR_PASSIVE_NODE | \
                                         CAN_FLAG_REACH_WARN_LIMIT)

#define CAN_FLAG_CLR_MSK                (CAN_FLAG_RB_OVF           | \
                                         CAN_FLAG_TRANS_ABORTED    | \
                                         CAN_FLAG_ERR_INT          | \
                                         CAN_FLAG_STB_TRANS_OK     | \
                                         CAN_FLAG_PTB_TRANS_OK     | \
                                         CAN_FLAG_RB_ALMOST_FULL   | \
                                         CAN_FLAG_RB_FIFO_FULL     | \
                                         CAN_FLAG_RX_OVERRUN       | \
                                         CAN_FLAG_RX_OK            | \
                                         CAN_FLAG_BUS_ERR          | \
                                         CAN_FLAG_ARB_LOST         | \
                                         CAN_FLAG_ERR_PASSIVE      | \
                                         CAN_FLAG_ERR_PASSIVE_NODE | \
                                         CAN_FLAG_REACH_WARN_LIMIT)

#define CAN_FLAG_TX_ERR_MSK             (CAN_FLAG_BUS_OFF          | \
                                         CAN_FLAG_TB_FULL          | \
                                         CAN_FLAG_ERR_INT          | \
                                         CAN_FLAG_BUS_ERR          | \
                                         CAN_FLAG_ARB_LOST         | \
                                         CAN_FLAG_ERR_PASSIVE      | \
                                         CAN_FLAG_ERR_PASSIVE_NODE | \
                                         CAN_FLAG_REACH_WARN_LIMIT)

/**
 * @}
 */

/**
 * @defgroup CAN_AF_Mask_Type CAN AF Mask Type
 * @{
 */
#define CAN_AF_MSK_STD_EXT              (0x0U)                  /*!< Acceptance filter accept standard ID mask and extended ID mask. */
#define CAN_AF_MSK_STD                  (CAN_ACF_AIDEE)         /*!< Acceptance filter accept standard ID mask. */
#define CAN_AF_MSK_EXT                  (CAN_ACF_AIDEE | \
                                         CAN_ACF_AIDE)          /*!< Acceptance filter accept extended ID mask. */
/**
 * @}
 */

/**
 * @defgroup CAN_RB_Status CAN Receive Buffer Status
 * @{
 */
#define CAN_RB_STAT_EMPTY               (0x0U)                  /*!< Receive buffer(RB) is empty. */
#define CAN_RB_STAT_LESS_WARN_LIMIT     (0x1U)                  /*!< RB is not empty, but is less than almost full warning limit. */
#define CAN_RB_STAT_MORE_WARN_LIMIT     (0x2U)                  /*!< RB is not full, but is more than or equal to almost full warning limit. */
#define CAN_RB_STAT_FULL                (0x3U)                  /*!< RB is full. */
/**
 * @}
 */

/**
 * @defgroup CAN_DLC CAN Data_Length_Code
 * @{
 */
#define CAN_DLC_0                       (0x0U)                    /*!< CAN2.0 and CANFD: payload is 0 in bytes. */
#define CAN_DLC_1                       (0x1U)                    /*!< CAN2.0 and CANFD: payload is 1 in bytes. */
#define CAN_DLC_2                       (0x2U)                    /*!< CAN2.0 and CANFD: payload is 2 in bytes. */
#define CAN_DLC_3                       (0x3U)                    /*!< CAN2.0 and CANFD: payload is 3 in bytes. */
#define CAN_DLC_4                       (0x4U)                    /*!< CAN2.0 and CANFD: payload is 4 in bytes. */
#define CAN_DLC_5                       (0x5U)                    /*!< CAN2.0 and CANFD: payload is 5 in bytes. */
#define CAN_DLC_6                       (0x6U)                    /*!< CAN2.0 and CANFD: payload is 6 in bytes. */
#define CAN_DLC_7                       (0x7U)                    /*!< CAN2.0 and CANFD: payload is 7 in bytes. */
#define CAN_DLC_8                       (0x8U)                    /*!< CAN2.0 and CANFD: payload is 8 in bytes. */
#define CAN_DLC_12                      (0x9U)                    /*!< CANFD: payload is 12 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_16                      (0xAU)                    /*!< CANFD: payload is 16 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_20                      (0xBU)                    /*!< CANFD: payload is 20 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_24                      (0xCU)                    /*!< CANFD: payload is 24 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_32                      (0xDU)                    /*!< CANFD: payload is 32 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_48                      (0xEU)                    /*!< CANFD: payload is 48 in bytes. CAN2.0: payload is 8 in bytes. */
#define CAN_DLC_64                      (0xFU)                    /*!< CANFD: payload is 64 in bytes. CAN2.0: payload is 8 in bytes. */
/**
 * @}
 */

/**
 * @defgroup CAN_AF CAN Acceptance Filter
 * @{
 */
#define CAN_AF1                         (CAN_ACFEN_AE_1)        /*!< Acceptance filter 1 select bit. */
#define CAN_AF2                         (CAN_ACFEN_AE_2)        /*!< Acceptance filter 2 select bit. */
#define CAN_AF3                         (CAN_ACFEN_AE_3)        /*!< Acceptance filter 3 select bit. */
#define CAN_AF4                         (CAN_ACFEN_AE_4)        /*!< Acceptance filter 4 select bit. */
#define CAN_AF5                         (CAN_ACFEN_AE_5)        /*!< Acceptance filter 5 select bit. */
#define CAN_AF6                         (CAN_ACFEN_AE_6)        /*!< Acceptance filter 6 select bit. */
#define CAN_AF7                         (CAN_ACFEN_AE_7)        /*!< Acceptance filter 7 select bit. */
#define CAN_AF8                         (CAN_ACFEN_AE_8)        /*!< Acceptance filter 8 select bit. */
#define CAN_AF9                         (CAN_ACFEN_AE_9)        /*!< Acceptance filter 9 select bit. */
#define CAN_AF10                        (CAN_ACFEN_AE_10)       /*!< Acceptance filter 10 select bit. */
#define CAN_AF11                        (CAN_ACFEN_AE_11)       /*!< Acceptance filter 11 select bit. */
#define CAN_AF12                        (CAN_ACFEN_AE_12)       /*!< Acceptance filter 12 select bit. */
#define CAN_AF13                        (CAN_ACFEN_AE_13)       /*!< Acceptance filter 13 select bit. */
#define CAN_AF14                        (CAN_ACFEN_AE_14)       /*!< Acceptance filter 14 select bit. */
#define CAN_AF15                        (CAN_ACFEN_AE_15)       /*!< Acceptance filter 15 select bit. */
#define CAN_AF16                        (CAN_ACFEN_AE_16)       /*!< Acceptance filter 16 select bit. */
#define CAN_AF_ALL                      (0xFFFFU)
/**
 * @}
 */

/**
 * @defgroup CAN_Trans_Mode CAN Transmission Mode
 * @{
 */
#define CAN_TRANS_PTB_STB_AUTO_RETX     (0x0U)                  /*!< Both PTB and STB automatically retransmit. */
#define CAN_TRANS_PTB_SSHOT             (0x10U)                 /*!< PTB single shot transmission mode, STB automatically retransmit. */
#define CAN_TRANS_STB_SSHOT             (0x08U)                 /*!< STB single shot transmission mode. PTB automatically retransmit. */
#define CAN_TRANS_PTB_STB_SSHOT         (0x10U | \
                                         0x08U)                 /*!< STB single shot, PTB single shot. */
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_Transmit_Buffer_Mode CAN Time-triggered Communication Transmit Buffer Mode
 * @{
 */
#define CAN_TTC_TB_MODE_NORMAL          (0x0U)                      /*!< TTC transmit buffer depends on the priority of STB which is defined by @ref CAN_STB_Priority_Mode */
#define CAN_TTC_TB_MODE_PTR             (CAN_TCTRL_TTTBM)           /*!< TTC transmit buffer is pointed by TBSLOT.TBPTR(for filling) and \
                                                                         TRG_CFG.TTPTR(for transmission). */
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_TBS_Pointer CAN Time-triggered Communication Transmit Buffer Slot Pointer
 * @{
 */
#define CAN_TTC_TBS_PTB                 (0x0U)                      /*!< Point to PTB. */
#define CAN_TTC_TBS_STB1                (0x1U)                      /*!< Point to STB slot 1. */
#define CAN_TTC_TBS_STB2                (0x2U)                      /*!< Point to STB slot 2. */
#define CAN_TTC_TBS_STB3                (0x3U)                      /*!< Point to STB slot 3. */
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_Status_Flag CAN Time-triggered Communication Status Flag
 * @{
 */
#define CAN_TTC_FLAG_TTI                (0x08U)                     /*!< Time trigger interrupt flag. */
#define CAN_TTC_FLAG_TEI                (0x20U)                     /*!< Trigger error interrupt flag. */
#define CAN_TTC_FLAG_WTI                (0x40U)                     /*!< Watch trigger interrupt flag. */

#define CAN_TTC_FLAG_ALL                (CAN_TTC_FLAG_TTI | \
                                         CAN_TTC_FLAG_TEI | \
                                         CAN_TTC_FLAG_WTI)
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_Interrupt_Type CAN Time-triggered Communication Interrupt Type
 * @{
 */
#define CAN_TTC_INT_TTI                 (0x10U)                     /*!< Time trigger interrupt. */
#define CAN_TTC_INT_WTI                 (0x80U)                     /*!< Watch trigger interrupt. */
#define CAN_TTC_INT_ALL                 (CAN_TTC_INT_TTI | CAN_TTC_INT_WTI)
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_NTU_Prescaler CAN Time-triggered Communication Network Time Unit Prescaler
 * @{
 */
#define CAN_TTC_NTU_PRESC_1             (0x0U)                                          /*!< NTU is SBT bit time * 1. */
#define CAN_TTC_NTU_PRESC_2             ((uint8_t)(0x1UL << 1U))                        /*!< NTU is SBT bit time * 2. */
#define CAN_TTC_NTU_PRESC_4             ((uint8_t)(0x2UL << 1U))                        /*!< NTU is SBT bit time * 4. */
#define CAN_TTC_NTU_PRESC_8             ((uint8_t)(0x3UL << 1U))                        /*!< NTU is SBT bit time * 8. */
/**
 * @}
 */

/**
 * @defgroup CAN_TTC_Trigger_Type CAN Time-triggered Communication Trigger Type
 * @{
 */
#define CAN_TTC_TRIG_IMMED_TRIG         (0x0U)                                          /*!< Immediate trigger for immediate transmission. */
#define CAN_TTC_TRIG_TIME_TRIG          ((uint16_t)(0x1UL << 8U))                       /*!< Time trigger for receive triggers. */
#define CAN_TTC_TRIG_SSHOT_TRANS_TRIG   ((uint16_t)(0x2UL << 8U))                       /*!< Single shot transmit trigger for exclusive time windows. */
#define CAN_TTC_TRIG_TRANS_START_TRIG   ((uint16_t)(0x3UL << 8U))                       /*!< Transmit start trigger for merged arbitrating time windows. */
#define CAN_TTC_TRIG_TRANS_STOP_TRIG    ((uint16_t)(0x4UL << 8U))                       /*!< Transmit stop trigger for merged arbitrating time windows. */
/**
 * @}
 */

/*******************************************************************************
                Bit definition for Peripheral CAN
*******************************************************************************/
/*  Bit definition for CAN_CFG_STAT register  */
#define CAN_CFG_STAT_BUSOFF_POS                        (0U)
#define CAN_CFG_STAT_BUSOFF                            (0x01U)
#define CAN_CFG_STAT_TACTIVE_POS                       (1U)
#define CAN_CFG_STAT_TACTIVE                           (0x02U)
#define CAN_CFG_STAT_RACTIVE_POS                       (2U)
#define CAN_CFG_STAT_RACTIVE                           (0x04U)
#define CAN_CFG_STAT_TSSS_POS                          (3U)
#define CAN_CFG_STAT_TSSS                              (0x08U)
#define CAN_CFG_STAT_TPSS_POS                          (4U)
#define CAN_CFG_STAT_TPSS                              (0x10U)
#define CAN_CFG_STAT_LBMI_POS                          (5U)
#define CAN_CFG_STAT_LBMI                              (0x20U)
#define CAN_CFG_STAT_LBME_POS                          (6U)
#define CAN_CFG_STAT_LBME                              (0x40U)
#define CAN_CFG_STAT_RESET_POS                         (7U)
#define CAN_CFG_STAT_RESET                             (0x80U)

/*  Bit definition for CAN_TCMD register  */
#define CAN_TCMD_TSA_POS                               (0U)
#define CAN_TCMD_TSA                                   (0x01U)
#define CAN_TCMD_TSALL_POS                             (1U)
#define CAN_TCMD_TSALL                                 (0x02U)
#define CAN_TCMD_TSONE_POS                             (2U)
#define CAN_TCMD_TSONE                                 (0x04U)
#define CAN_TCMD_TPA_POS                               (3U)
#define CAN_TCMD_TPA                                   (0x08U)
#define CAN_TCMD_TPE_POS                               (4U)
#define CAN_TCMD_TPE                                   (0x10U)
#define CAN_TCMD_LOM_POS                               (6U)
#define CAN_TCMD_LOM                                   (0x40U)
#define CAN_TCMD_TBSEL_POS                             (7U)
#define CAN_TCMD_TBSEL                                 (0x80U)

/*  Bit definition for CAN_TCTRL register  */
#define CAN_TCTRL_TSSTAT_POS                           (0U)
#define CAN_TCTRL_TSSTAT                               (0x03U)
#define CAN_TCTRL_TSSTAT_0                             (0x01U)
#define CAN_TCTRL_TSSTAT_1                             (0x02U)
#define CAN_TCTRL_TTTBM_POS                            (4U)
#define CAN_TCTRL_TTTBM                                (0x10U)
#define CAN_TCTRL_TSMODE_POS                           (5U)
#define CAN_TCTRL_TSMODE                               (0x20U)
#define CAN_TCTRL_TSNEXT_POS                           (6U)
#define CAN_TCTRL_TSNEXT                               (0x40U)
#define CAN_TCTRL_FD_ISO_POS                           (7U)
#define CAN_TCTRL_FD_ISO                               (0x80U)

/*  Bit definition for CAN_RCTRL register  */
#define CAN_RCTRL_RSTAT_POS                            (0U)
#define CAN_RCTRL_RSTAT                                (0x03U)
#define CAN_RCTRL_RSTAT_0                              (0x01U)
#define CAN_RCTRL_RSTAT_1                              (0x02U)
#define CAN_RCTRL_RBALL_POS                            (3U)
#define CAN_RCTRL_RBALL                                (0x08U)
#define CAN_RCTRL_RREL_POS                             (4U)
#define CAN_RCTRL_RREL                                 (0x10U)
#define CAN_RCTRL_ROV_POS                              (5U)
#define CAN_RCTRL_ROV                                  (0x20U)
#define CAN_RCTRL_ROM_POS                              (6U)
#define CAN_RCTRL_ROM                                  (0x40U)
#define CAN_RCTRL_SACK_POS                             (7U)
#define CAN_RCTRL_SACK                                 (0x80U)

/*  Bit definition for CAN_RTIE register  */
#define CAN_RTIE_TSFF_POS                              (0U)
#define CAN_RTIE_TSFF                                  (0x01U)
#define CAN_RTIE_EIE_POS                               (1U)
#define CAN_RTIE_EIE                                   (0x02U)
#define CAN_RTIE_TSIE_POS                              (2U)
#define CAN_RTIE_TSIE                                  (0x04U)
#define CAN_RTIE_TPIE_POS                              (3U)
#define CAN_RTIE_TPIE                                  (0x08U)
#define CAN_RTIE_RAFIE_POS                             (4U)
#define CAN_RTIE_RAFIE                                 (0x10U)
#define CAN_RTIE_RFIE_POS                              (5U)
#define CAN_RTIE_RFIE                                  (0x20U)
#define CAN_RTIE_ROIE_POS                              (6U)
#define CAN_RTIE_ROIE                                  (0x40U)
#define CAN_RTIE_RIE_POS                               (7U)
#define CAN_RTIE_RIE                                   (0x80U)

/*  Bit definition for CAN_RTIF register  */
#define CAN_RTIF_AIF_POS                               (0U)
#define CAN_RTIF_AIF                                   (0x01U)
#define CAN_RTIF_EIF_POS                               (1U)
#define CAN_RTIF_EIF                                   (0x02U)
#define CAN_RTIF_TSIF_POS                              (2U)
#define CAN_RTIF_TSIF                                  (0x04U)
#define CAN_RTIF_TPIF_POS                              (3U)
#define CAN_RTIF_TPIF                                  (0x08U)
#define CAN_RTIF_RAFIF_POS                             (4U)
#define CAN_RTIF_RAFIF                                 (0x10U)
#define CAN_RTIF_RFIF_POS                              (5U)
#define CAN_RTIF_RFIF                                  (0x20U)
#define CAN_RTIF_ROIF_POS                              (6U)
#define CAN_RTIF_ROIF                                  (0x40U)
#define CAN_RTIF_RIF_POS                               (7U)
#define CAN_RTIF_RIF                                   (0x80U)

/*  Bit definition for CAN_ERRINT register  */
#define CAN_ERRINT_BEIF_POS                            (0U)
#define CAN_ERRINT_BEIF                                (0x01U)
#define CAN_ERRINT_BEIE_POS                            (1U)
#define CAN_ERRINT_BEIE                                (0x02U)
#define CAN_ERRINT_ALIF_POS                            (2U)
#define CAN_ERRINT_ALIF                                (0x04U)
#define CAN_ERRINT_ALIE_POS                            (3U)
#define CAN_ERRINT_ALIE                                (0x08U)
#define CAN_ERRINT_EPIF_POS                            (4U)
#define CAN_ERRINT_EPIF                                (0x10U)
#define CAN_ERRINT_EPIE_POS                            (5U)
#define CAN_ERRINT_EPIE                                (0x20U)
#define CAN_ERRINT_EPASS_POS                           (6U)
#define CAN_ERRINT_EPASS                               (0x40U)
#define CAN_ERRINT_EWARN_POS                           (7U)
#define CAN_ERRINT_EWARN                               (0x80U)

/*  Bit definition for CAN_LIMIT register  */
#define CAN_LIMIT_EWL_POS                              (0U)
#define CAN_LIMIT_EWL                                  (0x0FU)
#define CAN_LIMIT_AFWL_POS                             (4U)
#define CAN_LIMIT_AFWL                                 (0xF0U)

/*  Bit definition for CAN_SBT register  */
#define CAN_SBT_S_SEG_1_POS                            (0U)
#define CAN_SBT_S_SEG_1                                (0x000000FFUL)
#define CAN_SBT_S_SEG_2_POS                            (8U)
#define CAN_SBT_S_SEG_2                                (0x00007F00UL)
#define CAN_SBT_S_SJW_POS                              (16U)
#define CAN_SBT_S_SJW                                  (0x007F0000UL)
#define CAN_SBT_S_PRESC_POS                            (24U)
#define CAN_SBT_S_PRESC                                (0xFF000000UL)

/*  Bit definition for CAN_FBT register  */
#define CAN_FBT_F_SEG_1_POS                            (0U)
#define CAN_FBT_F_SEG_1                                (0x0000001FUL)
#define CAN_FBT_F_SEG_2_POS                            (8U)
#define CAN_FBT_F_SEG_2                                (0x00000F00UL)
#define CAN_FBT_F_SJW_POS                              (16U)
#define CAN_FBT_F_SJW                                  (0x000F0000UL)
#define CAN_FBT_F_PRESC_POS                            (24U)
#define CAN_FBT_F_PRESC                                (0xFF000000UL)

/*  Bit definition for CAN_EALCAP register  */
#define CAN_EALCAP_ALC_POS                             (0U)
#define CAN_EALCAP_ALC                                 (0x1FU)
#define CAN_EALCAP_KOER_POS                            (5U)
#define CAN_EALCAP_KOER                                (0xE0U)

/*  Bit definition for CAN_TDC register  */
#define CAN_TDC_SSPOFF_POS                             (0U)
#define CAN_TDC_SSPOFF                                 (0x7FU)
#define CAN_TDC_TDCEN_POS                              (7U)
#define CAN_TDC_TDCEN                                  (0x80U)

/*  Bit definition for CAN_RECNT register  */
#define CAN_RECNT_MASK                                 (0xFFU)

/*  Bit definition for CAN_TECNT register  */
#define CAN_TECNT_MASK                                 (0xFFU)

/*  Bit definition for CAN_ACFCTRL register  */
#define CAN_ACFCTRL_ACFADR_POS                         (0U)
#define CAN_ACFCTRL_ACFADR                             (0x0FU)
#define CAN_ACFCTRL_SELMASK_POS                        (5U)
#define CAN_ACFCTRL_SELMASK                            (0x20U)

/*  Bit definition for CAN_ACFEN register  */
#define CAN_ACFEN_AE_1_POS                             (0U)
#define CAN_ACFEN_AE_1                                 (0x0001U)
#define CAN_ACFEN_AE_2_POS                             (1U)
#define CAN_ACFEN_AE_2                                 (0x0002U)
#define CAN_ACFEN_AE_3_POS                             (2U)
#define CAN_ACFEN_AE_3                                 (0x0004U)
#define CAN_ACFEN_AE_4_POS                             (3U)
#define CAN_ACFEN_AE_4                                 (0x0008U)
#define CAN_ACFEN_AE_5_POS                             (4U)
#define CAN_ACFEN_AE_5                                 (0x0010U)
#define CAN_ACFEN_AE_6_POS                             (5U)
#define CAN_ACFEN_AE_6                                 (0x0020U)
#define CAN_ACFEN_AE_7_POS                             (6U)
#define CAN_ACFEN_AE_7                                 (0x0040U)
#define CAN_ACFEN_AE_8_POS                             (7U)
#define CAN_ACFEN_AE_8                                 (0x0080U)
#define CAN_ACFEN_AE_9_POS                             (8U)
#define CAN_ACFEN_AE_9                                 (0x0100U)
#define CAN_ACFEN_AE_10_POS                            (9U)
#define CAN_ACFEN_AE_10                                (0x0200U)
#define CAN_ACFEN_AE_11_POS                            (10U)
#define CAN_ACFEN_AE_11                                (0x0400U)
#define CAN_ACFEN_AE_12_POS                            (11U)
#define CAN_ACFEN_AE_12                                (0x0800U)
#define CAN_ACFEN_AE_13_POS                            (12U)
#define CAN_ACFEN_AE_13                                (0x1000U)
#define CAN_ACFEN_AE_14_POS                            (13U)
#define CAN_ACFEN_AE_14                                (0x2000U)
#define CAN_ACFEN_AE_15_POS                            (14U)
#define CAN_ACFEN_AE_15                                (0x4000U)
#define CAN_ACFEN_AE_16_POS                            (15U)
#define CAN_ACFEN_AE_16                                (0x8000U)

/*  Bit definition for CAN_ACF register  */
#define CAN_ACF_ACODEORAMASK_POS                       (0U)
#define CAN_ACF_ACODEORAMASK                           (0x1FFFFFFFUL)
#define CAN_ACF_AIDE_POS                               (29U)
#define CAN_ACF_AIDE                                   (0x20000000UL)
#define CAN_ACF_AIDEE_POS                              (30U)
#define CAN_ACF_AIDEE                                  (0x40000000UL)

/*  Bit definition for CAN_TBSLOT register  */
#define CAN_TBSLOT_TBPTR_POS                           (0U)
#define CAN_TBSLOT_TBPTR                               (0x3FU)
#define CAN_TBSLOT_TBF_POS                             (6U)
#define CAN_TBSLOT_TBF                                 (0x40U)
#define CAN_TBSLOT_TBE_POS                             (7U)
#define CAN_TBSLOT_TBE                                 (0x80U)

/*  Bit definition for CAN_TTCFG register  */
#define CAN_TTCFG_TTEN_POS                             (0U)
#define CAN_TTCFG_TTEN                                 (0x01U)
#define CAN_TTCFG_T_PRESC_POS                          (1U)
#define CAN_TTCFG_T_PRESC                              (0x06U)
#define CAN_TTCFG_TTIF_POS                             (3U)
#define CAN_TTCFG_TTIF                                 (0x08U)
#define CAN_TTCFG_TTIE_POS                             (4U)
#define CAN_TTCFG_TTIE                                 (0x10U)
#define CAN_TTCFG_TEIF_POS                             (5U)
#define CAN_TTCFG_TEIF                                 (0x20U)
#define CAN_TTCFG_WTIF_POS                             (6U)
#define CAN_TTCFG_WTIF                                 (0x40U)
#define CAN_TTCFG_WTIE_POS                             (7U)
#define CAN_TTCFG_WTIE                                 (0x80U)

/*  Bit definition for CAN_REF_MSG register  */
#define CAN_REF_MSG_REF_ID_POS                         (0U)
#define CAN_REF_MSG_REF_ID                             (0x1FFFFFFFUL)
#define CAN_REF_MSG_REF_IDE_POS                        (31U)
#define CAN_REF_MSG_REF_IDE                            (0x80000000UL)

/*  Bit definition for CAN_TRG_CFG register  */
#define CAN_TRG_CFG_TTPTR_POS                          (0U)
#define CAN_TRG_CFG_TTPTR                              (0x003FU)
#define CAN_TRG_CFG_TTYPE_POS                          (8U)
#define CAN_TRG_CFG_TTYPE                              (0x0700U)
#define CAN_TRG_CFG_TEW_POS                            (12U)
#define CAN_TRG_CFG_TEW                                (0xF000U)

/*  Bit definition for CAN_TT_TRIG register  */
#define CAN_TT_TRIG                                    (0xFFFFU)

/*  Bit definition for CAN_TT_WTRIG register  */
#define CAN_TT_WTRIG                                   (0xFFFFU)

const static uint8_t u8DLC2Size[2U][16U] =
{
    {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 8U, 8U, 8U, 8U, 8U, 8U, 8U},
    {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U},
};

/*******************************************************************************
 * Global variable definitions ('extern')
 ******************************************************************************/

extern unsigned char B_CanRead;     //CAN1 收到数据标志
extern unsigned char B_CanSend;     //CAN1 发送数据标志
extern unsigned char B_Can2Read;    //CAN2 收到数据标志
extern unsigned char B_Can2Send;    //CAN2 发送数据标志

extern stc_can_rx_t pstcRx[8];

/*******************************************************************************
  Global function prototypes (definition in C source)
 ******************************************************************************/
/**
 * @addtogroup CAN_Global_Functions
 * @{
 */

void CANFD_Init(CANFD_TypeDef xdata* CANx);
/* G5 xdata-param: not in demo-44 live call closure; param kept AS0 (G5-ADDRSPACE-DESIGN 3.2) */
void CAN_TransData(CANFD_TypeDef* CANx);
void CAN_SendData(CANFD_TypeDef xdata* CANx, stc_can_tx_t* pcanTx);
uint8_t CAN_ReceiveData(CANFD_TypeDef xdata* CANx, stc_can_rx_t* pstcRx);

/**
 * @}
 */

#endif