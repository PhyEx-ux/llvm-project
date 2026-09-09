#include "hardware.h"
void timer0(void) __attribute__((interrupt(1)));
void timer0(void)
{
    TCON &= 0xef;
    volatile u8 local[8];
    u8 sum = 0;
    for (u8 i = 0; i < 8; ++i) {
        local[i] = (u8)(SEED + i);
        sum = (u8)(sum + local[i]);
    }
    RESULT = helper(sum);
    SHARED |= 0x40;
    HITS++;
    DONE = 1;
}
