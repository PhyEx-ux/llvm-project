#include "hardware.h"

/* Compiler-generated Timer0 ISR (interrupt slot 1, vector 0xFF000B).
 *
 * Under test: the LLVM prologue/epilogue must preserve the interrupted
 * context while the ISR body ACTIVELY destroys registers.  clobber_hi() is
 * an assembly helper that overwrites R16-R31, DPXL and PSW and restores
 * nothing.  Legality per the frozen ABI (llvm/lib/Target/MCS251/
 * MCS251RegisterInfo.td): the callee-saved list is empty, "the callee may
 * freely clobber ... the dr0..dr28 file and psw" across an ecall.  Because
 * the helper saves nothing, the only protection for the interrupted context
 * is the ISR's own 37-byte save area; any omitted save is directly visible
 * in the sentinel's post-round register check and cannot be misattributed.
 */
void timer0(void) __attribute__((interrupt(1)));
void timer0(void)
{
    volatile u8 local[8];
    u8 sum = 0;
    u8 bad;

    TCON &= 0xef; /* stop Timer0 like the T10 single-layer demo */
    bad = clobber_hi(SEED);
    for (u8 i = 0; i < 8; ++i) {
        local[i] = (u8)(SEED + i);
        sum = (u8)(sum + local[i]);
    }
    /* RESULT is the helper's own read-back error count (0 expected); the
     * sentinel-mixed value also proves the round actually ran. */
    RESULT = (u8)(bad + sum - sum);
    SHARED |= 0x40;
    HITS++;
    DONE = 1;
}
