#include "hardware.h"

/* Timer0 ISR -- the HIGH-priority level (IP.PT0=1, IPH.PT0=1 -> level 3).
 * It must preempt the Timer1 ISR while Timer1 sits inside its window,
 * record where it hit, and return so Timer1 can resume. */
void timer0(void) __attribute__((interrupt(1)));
void timer0(void)
{
    volatile u8 local[8];
    u8 sum = 0;

    TCON &= 0xef; /* stop Timer0: fire exactly once per round */
    HITS0++;
    WPHASE = PHASE; /* preemption-window evidence: 1 = inside the window */
    LOG(LOGP) = 0xb1;
    LOGP = (u8)(LOGP + 1);
    for (u8 i = 0; i < 8; ++i) {
        local[i] = (u8)(SEED + i);
        sum = (u8)(sum + local[i]);
    }
    BYTE(0x107) = sum; /* unused by checks; keeps the pressure real */
    LOG(LOGP) = 0xb2;
    LOGP = (u8)(LOGP + 1);
}
