/*
 * Oracle-A host driver for pilot-nec-decode.
 * Feeds the sample stream one tick at a time; on each B_IR_Press rising
 * edge prints the decoded frame state as checkpoints:
 *   p<HEX8 B_IR_Press> u<HEX16 UserCode> k<HEX8 IR_code>
 *   H<HEX8 IR_UserH>   L<HEX8 IR_UserL> d<HEX8 IR_data>
 * Stream layout matches the firmware wrapper exactly: 'B' + frames + "PASS\n".
 */
#include <stdio.h>

#include "vectors.h"
#include "kernel.c"

/* kernel state lives in the driver (see kernel.c) */
u8 g_ir_level;
u8 IR_SampleCnt, IR_BitCnt, IR_UserH, IR_UserL, IR_data, IR_DataShift;
u8 P_IR_RX_temp, B_IR_Sync, B_IR_Press, IR_code;
u16 UserCode;

int main(void)
{
    int i;
    u8 last_press = 0;

    putchar('B');
    for (i = 0; i < NEC_SAMPLE_COUNT; i++) {
        g_ir_level = NEC_SAMPLES[i];
        IR_RX_NEC();
        if (B_IR_Press && !last_press) {
            printf("p%02X", B_IR_Press);
            printf("u%04X", UserCode);
            printf("k%02X", IR_code);
            printf("H%02X", IR_UserH);
            printf("L%02X", IR_UserL);
            printf("d%02X", IR_data);
            B_IR_Press = 0;   /* demo contract: app clears the flag after use */
        }
        last_press = B_IR_Press;
    }
    puts("PASS");
    return 0;
}
