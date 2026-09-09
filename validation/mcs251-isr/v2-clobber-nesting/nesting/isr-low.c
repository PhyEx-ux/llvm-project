#include "hardware.h"

/* Timer1 ISR -- the LOW-priority level (IP.PT1=1, IPH.PT1=0 -> level 1).
 * It opens a deterministic preemption window by starting Timer0 (the HIGH
 * ISR, level 3) inside its own body, spins long enough for Timer0 to
 * overflow, and computes a result AFTER the nested high ISR has returned.
 * The log order must be L1, H1, H2, L2. */
void timer1(void) __attribute__((interrupt(3)));
void timer1(void)
{
    TCON &= 0xbf; /* clear TR1 (bit 6): fire exactly once per round */
    HITS1++;
    LOG(LOGP) = 0xa1;
    LOGP = (u8)(LOGP + 1);
    PHASE = 1;
    TCON |= 0x10; /* TR0=1: the high ISR becomes pending inside the window */
    {
        /* Hold the window open until the high ISR has actually entered
         * (HITS0 set by Timer0's ISR), with a generous bound so a dead
         * Timer0 fails the round's HITS0 check instead of hanging. */
        volatile u16 bound = 0x8000;
        while (HITS0 == 0 && bound) { --bound; }
    }
    PHASE = 2;
    TCON &= 0xef; /* TR0=0; the high ISR also stops itself on entry */
    RESL = (u8)(0x3c ^ SEED); /* proves Timer1 resumed with its context */
    LOG(LOGP) = 0xa2;
    LOGP = (u8)(LOGP + 1);
    DONE = 1;
}
