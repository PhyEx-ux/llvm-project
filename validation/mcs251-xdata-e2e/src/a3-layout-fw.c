/*
 * a3-layout-fw.c - R4/R9 QEMU pairing for the layout/dataflow acceptance.
 *
 * Companion to llvm/test/CodeGen/MCS251/code-addrspacecast-datalayout.ll
 * (RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充二" §2.2).  The IR half proves
 * the target's own p0/p4 layout is 32/8 for both spaces and that the
 * conversion is value-preserving; this firmware proves the same facts on
 * silicon/simulator with the actual relocation-applied addresses:
 *
 *   1  ADDRESS equal: the numeric address of a CODE pointer (AS4) equals the
 *      numeric address of its converted AS0 view, for a table whose address
 *      comes from a relocation (not a folded constant);
 *   2  BYTE equal: reading each byte directly through AS4 and through the
 *      converted AS0 pointer yields identical bytes for the whole table;
 *   3  dynamic one-way conversion: an integer carrying a runtime-computed
 *      address round-trips through AS0 and back to AS4 unchanged (the
 *      "no tag/mask/truncate" claim, measured at run time);
 *   4  a two-byte (i16) read through both views agrees, pinning the width;
 *   5  the CODE table stays identifiable: its bytes match the compile-time
 *      initializer while read through the converted view.
 *
 * O2 DYNAMIC OBSERVATION (Alice review R9-2): the CODE table and its address
 * used to live in this TU, so at -O2 the optimizer resolved the address and
 * folded checks 2/4/5 to constant `1` and check 3 into a relocation-constant
 * comparison -- the five OKs were not a run-time data-flow measurement at
 * all.  The table now lives in a SEPARATE TU (src/a3-layout-tab.c) and is
 * reached only through the `noinline`-style opaque accessor `lay_base()`, so
 * the address is unknown at compile time in this TU and the `addrspacecast`
 * and the loads survive optimization as real instructions.  The e2e driver
 * asserts that survival on the -O2 IR.
 *
 * The table address is the address after linking (bank + 16-bit offset), so
 * a conversion that truncated to 16 bits or applied a tag would fail 1/3.
 */
#include "mcs251_type_compat.h"

#define SBUF (*(volatile BYTE *)0x99)

/* CODE-resident table, defined in the separate TU a3-layout-tab.c.  Reached
 * through an accessor declared here and defined there: this is what keeps the
 * address dynamic under -O2 (a locally defined object is resolved and the
 * whole comparison constant-folded). */
extern const BYTE code *lay_base(void);

__attribute__((noinline)) static void uputc(char c) { SBUF = (BYTE)c; }

__attribute__((noinline)) static void uputs(const char *s)
{
    while (*s)
        uputc(*s++);
}

__attribute__((noinline)) static void report2(const char *tag, int ok)
{
    uputs(ok ? "OK" : "F");
    uputs(tag);
    uputc(' ');
}

/* The conversion under test must not change the numeric address. */
__attribute__((noinline)) static u32 addr_of_as4(const BYTE __code *p)
{
    return (u32)p;
}

__attribute__((noinline)) static u32 addr_of_as0(const BYTE *p)
{
    return (u32)p;
}

int main(void)
{
    unsigned fails = 0;
    unsigned i;
    const BYTE __code *b4 = lay_base();
    const BYTE *plain = (const BYTE *)b4;

    /* 1: the numeric address survives the conversion. */
    {
        u32 a4 = addr_of_as4(b4);
        u32 a0 = addr_of_as0(plain);
        report2("1", a4 == a0);
        fails += (a4 != a0);
    }

    /* 2: byte-for-byte equality across both views. */
    {
        int ok = 1;
        for (i = 0; i < 8u; ++i)
            ok &= (b4[i] == plain[i]);
        report2("2", ok);
        fails += !ok;
    }

    /* 3: dynamic one-way conversion of a runtime address. */
    {
        const BYTE __code *back = (const BYTE __code *)plain;
        u32 a0 = (u32)plain;
        u32 a4 = (u32)back;
        unsigned off = (unsigned)back - (unsigned)b4;
        report2("3", a0 == a4 && off == 0u);
        fails += (a0 != a4) || (off != 0u);
    }

    /* 4: two-byte read through both views. */
    {
        unsigned direct = (unsigned)b4[2] | ((unsigned)b4[3] << 8);
        unsigned via = (unsigned)plain[2] | ((unsigned)plain[3] << 8);
        report2("4", direct == via && direct == 0x7654u);
        fails += (direct != via) || (direct != 0x7654u);
    }

    /* 5: the payload bytes are the compile-time initializer, read through the
     *    converted view (the direct view is checked by 2/4). */
    {
        int ok = 1;
        for (i = 0; i < 8u; ++i)
            ok &= (plain[i] == (BYTE)(0x10u + 0x22u * i));
        report2("5", ok);
        fails += !ok;
    }

    uputs("\n");
    uputs(fails ? "A3-LAYOUT-FAIL\n" : "A3-LAYOUT-PASS\n");
    for (;;)
        ;
}
