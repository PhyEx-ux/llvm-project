/*
 * T1 pilot kernel: delay_ms -- busy-wait delay (compile+termination smoke).
 *
 * Source: STC32G144K246 demo 00,
 *   00-端口模式设置/C语言/main.c:118
 *
 * Per DESIGN.md section 2-T1, delay-class kernels get NO behavioural oracle:
 * the case is a "compiles and terminates" smoke -- the harness must reach
 * PASS on QEMU within the timeout, proving the empty loops survived codegen
 * (no dead-code elimination of the counted loops) and execution completes.
 *
 * Rewrite: u16 typedef; MAIN_Fosc/6000 = 48000000/6000 = 8000 folded to a
 * literal (a u32/u32 division at -O0 would lower to a runtime helper call
 * the bare-metal DUT link cannot resolve; the divisor folding is exactly
 * what any optimising front end does with these constants).  The loop
 * structure -- do { while(--i); } while(--ms); -- is kept verbatim, which is
 * the shape under regression here.
 */

typedef unsigned char u8;
typedef unsigned short u16;

void delay_ms(u16 ms)
{
    u16 i;
    do {
        i = 8000;       /* MAIN_Fosc / 6000 at 48MHz */
        while (--i);
    } while (--ms);
}
