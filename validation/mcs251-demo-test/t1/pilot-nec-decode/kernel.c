/*
 * T1 pilot kernel: IR_RX_NEC -- NEC infrared decode state machine
 * (sync hunt + bit-shift decode + code-pair verification).
 *
 * Source: STC32G144K246 demo 27,
 *   27-红外遥控接收程序(NEC码)-数码管显示用户地址和键值/C语言/main.c:165
 * Extracted from timer0's ISR body (the approved "zero-arg ISR state
 * machine" route): IR_RX_NEC() is the whole per-tick decoder; its caller in
 * the demo is `void timer0(void) interrupt 1 { IR_RX_NEC(); ... }`, which we
 * drop together with the interrupt attribute -- the kernel itself is a
 * plain zero-arg function with all state in globals, exactly as in the demo.
 *
 * Rewrite per DESIGN.md section 2-T1 (types only, semantics unchanged):
 *   - bit -> u8 (P_IR_RX_temp, B_IR_Sync, B_IR_Press)
 *   - PSW F0 scratch flag -> local u8
 *   - input `#define P_IR_RX (PBIN & 0x40)` (SFR read) -> global g_ir_level,
 *     normalized to 0/1 by the kernel itself
 *   - timing thresholds folded to literals for SysTick=10000, MAIN_Fosc=48MHz
 *     (demo 27 main.c:31-34,149-160): sample=100us, SYNC_MAX=15000/100=150,
 *     SYNC_MIN=9700/100=97, SYNC_DIVIDE=12375/100=123, DATA_MAX=3000/100=30,
 *     DATA_MIN=600/100=6, DATA_DIVIDE=1687/100=16, D_IR_BIT_NUMBER=32.
 *     Integer division truncation is what the demo's preprocessor does too.
 *   - `~IR_DataShift == IR_data` (data/inverse-data check) rewritten as
 *     `(u8)(~IR_DataShift) == IR_data`: under C integer promotion the
 *     original compares a negative int against an unsigned char and is
 *     always false on host compilers, while Keil C251 evaluates it in 8
 *     bits.  The explicit cast pins the intended 8-bit semantics on all
 *     three compilers; the decode logic itself is untouched.
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* Storage note: extern declarations only -- the MCS251 backend rejects
 * defined global data (Phase 12 Step 2 pending); the drivers define the
 * state (wrapper.c on target, host-main.c on host).  In the demo these
 * globals belong to the IR module's translation unit; ownership moves to
 * the drivers here.  Power-on zero matches the demo's assumption (QEMU
 * RAM is zeroed). */
extern u8 g_ir_level;         /* was: P_IR_RX == (PBIN & 0x40) */

extern u8 IR_SampleCnt;       /* sample counter */
extern u8 IR_BitCnt;          /*编码位数*/
extern u8 IR_UserH;           /*用户码(地址)高字节*/
extern u8 IR_UserL;           /*用户码(地址)低字节*/
extern u8 IR_data;            /*数据原码*/
extern u8 IR_DataShift;       /*数据移位*/

extern u8 P_IR_RX_temp;       /* was: bit -- Last sample */
extern u8 B_IR_Sync;          /* was: bit -- got SYNC */
extern u8 B_IR_Press;         /* was: bit -- frame ready */
extern u8 IR_code;            /*红外键码*/
extern u16 UserCode;          /*用户码*/

void IR_RX_NEC(void)
{
    u8 SampleTime;
    u8 F0;                                  /* was: PSW F0 */

    IR_SampleCnt++;                         /*Sample + 1*/

    F0 = P_IR_RX_temp;                      /*Save Last sample status*/
    P_IR_RX_temp = (g_ir_level != 0) ? 1 : 0;   /*Read current status*/
    if (F0 && !P_IR_RX_temp)                /*falling edge*/
    {
        SampleTime = IR_SampleCnt;          /*get the sample time*/
        IR_SampleCnt = 0;                   /*Clear the sample counter*/

        if (SampleTime > 150)     B_IR_Sync = 0;    /*too long: error*/
        else if (SampleTime >= 97)                  /*SYNC window*/
        {
            if (SampleTime >= 123)
            {
                B_IR_Sync = 1;                  /*has received SYNC*/
                IR_BitCnt = 32;                 /*Load bit number*/
            }
        }
        else if (B_IR_Sync)                        /*has received SYNC*/
        {
            if (SampleTime > 30)      B_IR_Sync = 0;   /*data sample too large*/
            else
            {
                IR_DataShift >>= 1;                 /*data shift right 1 bit*/
                if (SampleTime >= 16)  IR_DataShift |= 0x80;    /*data 0 or 1*/
                if (--IR_BitCnt == 0)                /*bit number is over?*/
                {
                    B_IR_Sync = 0;                  /*Clear SYNC*/
                    if ((u8)(~IR_DataShift) == IR_data)    /*正反码校验*/
                    {
                        UserCode = ((u16)IR_UserH << 8) + IR_UserL;
                        IR_code      = IR_data;
                        B_IR_Press   = 1;           /*数据有效*/
                    }
                }
                else if ((IR_BitCnt & 7) == 0)        /*one byte receive*/
                {
                    IR_UserL = IR_UserH;            /*Save the User code high byte*/
                    IR_UserH = IR_data;             /*Save the User code low byte*/
                    IR_data  = IR_DataShift;        /*Save the IR data byte*/
                }
            }
        }
    }
}
