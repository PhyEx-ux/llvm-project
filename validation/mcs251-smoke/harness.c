/*
 * MCS-251 SDCC/QEMU/LLVM integration smoke harness.
 *
 * The harness is always compiled by SDCC.  The mcs251_probe() implementation
 * is first compiled by SDCC, then replaced by an object assembled from LLVM's
 * generated MCS-251 assembly.  Seeing PASS from both images proves that the
 * generated return instruction is accepted by the SDCC assembler/linker and
 * executes correctly on the QEMU MCS-251 machine.
 */

__sfr __at (0x99) SBUF;
__xdata __at (0x010020) volatile unsigned char call_guard;

extern void mcs251_probe(void);

void
main(void)
{
    call_guard = 0x5a;

    /* This byte is emitted before the external call.  If RET is broken, the
       trailing PASS marker is never reached. */
    SBUF = 'B';
    mcs251_probe();

    if (call_guard == 0x5a)
        {
            SBUF = 'P';
            SBUF = 'A';
            SBUF = 'S';
            SBUF = 'S';
            SBUF = '\n';
        }
    else
        {
            SBUF = 'F';
            SBUF = 'A';
            SBUF = 'I';
            SBUF = 'L';
            SBUF = '\n';
        }

    for (;;)
        {
        }
}
