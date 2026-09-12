/*
 * mcs251-as4-cross-tu-fw.c - QEMU firmware for the A2c cross-TU test.
 *
 * Links against the separately compiled caller and callee objects and runs
 * under QEMU (see the RUN lines in mcs251-as4-cross-tu.c).  It proves the
 * ABI agreement at run time: the CODE table bytes are read back through (a)
 * a CODE pointer returned across TUs, (b) a code pointer parameter sent
 * across TUs, and (c) a converted generic AS0 pointer -- all three must see
 * the same table.
 *
 * The harness only uses the supported v1 ABI shapes (single pointer first
 * parameter, scalar/pointer return); no follow-on pointer parameters, which
 * belong to the v2 ABI slice.
 */
#include "mcs251-as4-cross-tu.h"

#define SBUF (*(volatile unsigned char *)0x99)

__attribute__((noinline)) static void uputc(char c) { SBUF = (unsigned char)c; }

__attribute__((noinline)) static void uputs(const char *s)
{
    while (*s)
        uputc(*s++);
}

int main(void)
{
    unsigned fails = 0;
    unsigned i;

    /* 1: the CODE table is visible across the TU boundary through an AS4
     *    pointer returned by the callee. */
    const uint8 __code *base = code_base();
    for (i = 0; i < 8u; ++i)
        fails += (base[i] != (uint8)(10u * (i + 1u)));

    /* 2: a CODE pointer parameter crosses the boundary and reads the right
     *    element. */
    fails += (code_read(shared_rom, 5u) != 60u);
    const uint8 __code *slot = code_lookup(3u);
    fails += (*slot != 40u);

    /* 3: the same CODE source converted to the generic AS0 pointer reaches
     *    the callee's AS0 entry point and reads the same byte. */
    const uint8 *plain = shared_rom;
    fails += (generic_read(plain, 6u) != 70u);
    fails += (generic_read(base, 2u) != 30u);

    uputs(fails ? "A2C-FAIL\n" : "A2C-PASS\n");
    for (;;)
        ;
}
