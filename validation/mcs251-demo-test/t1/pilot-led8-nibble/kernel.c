/*
 * T1 pilot kernel: led8_update -- user-code/ir-code nibble unpack into the
 * 7-segment display buffer.
 *
 * Source: STC32G144K246 demo 27 (NEC IR receiver + 7-seg display),
 *   27-红外遥控接收程序(NEC码)-数码管显示用户地址和键值/C语言/main.c:106-116
 * Extracted from main()'s while(1) body (the B_IR_Press branch) per the
 * approved "main-loop body extraction" route: the display-register writes
 * stay in the demo, the pure computation over globals becomes this kernel.
 *
 * Rewrite per DESIGN.md section 2-T1 (types only, semantics unchanged):
 *   - bit -> u8 (B_IR_Press)
 *   - faithful u16 UserCode form (baseline reset 2026-09-05: the earlier
 *     byte-split workaround targeted the trunc(load i16) miscompile fixed
 *     by commit 1c59ad49f; the refrozen llc md5 67a17057 reads the SDCC-
 *     written u16 correctly, so the demo's native shape is restored)
 *   - DisplayScan()/SFR access not extracted (T2 territory)
 */

typedef unsigned char u8;
typedef unsigned short u16;

/* Storage note: extern declarations only -- the MCS251 backend rejects
 * defined global data (Phase 12 Step 2 pending); the driver defines them
 * (wrapper.c on target, host-main.c on host).  All-zero init on both
 * sides matches the demo's power-on assumption (QEMU RAM is zeroed). */
extern u8  LED8[8];         /* display buffer: nibble indices per digit */
extern u16 UserCode;        /* decoded IR user code */
extern u8  IR_code;         /* decoded IR key code */
extern u8  B_IR_Press;      /* was: bit */

void led8_update(void)
{
    if (B_IR_Press)          /* a key code has arrived */
    {
        B_IR_Press = 0;

        LED8[0] = (u8)((UserCode >> 12) & 0x0f);   /* user code high byte, high nibble */
        LED8[1] = (u8)((UserCode >> 8)  & 0x0f);   /* user code high byte, low nibble  */
        LED8[2] = (u8)((UserCode >> 4)  & 0x0f);   /* user code low byte, high nibble  */
        LED8[3] = (u8)(UserCode & 0x0f);           /* user code low byte, low nibble   */
        LED8[6] = (u8)(IR_code >> 4);
        LED8[7] = (u8)(IR_code & 0x0f);
    }
}
