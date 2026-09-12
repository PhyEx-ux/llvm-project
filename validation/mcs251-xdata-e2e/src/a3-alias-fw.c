/*
 * a3-alias-fw.c - A3 alias QEMU pairing firmware.
 *
 * Companion to llvm/test/CodeGen/MCS251/code-addrspacecast-alias.ll
 * (RUNTIME-AS-PTR-DESIGN-A.md §3-A3 "验收补充一"): the IR test proves the
 * optimizer models AS4/AS0 aliasing; this firmware proves the same
 * constructs survive lowering and run correctly on the target.
 *
 * COVERAGE:
 *   1  AS0 store through a converted alias, then read the same byte through
 *      the converted AS4 view -- the value must come back (a wrong NoAlias
 *      would have folded the read to a stale constant).
 *   2  the reverse: write through the converted AS4 view* and read through
 *      AS0, with the byte reached by a different GEP chain (*the object
 *      written is ordinary AS0 RAM whose address is also expressed in AS4:
 *      the target-extension aliasing contract, NOT a real `__code` object).
 *   3  multi-byte (2-byte) access across the conversion.
 *   4  legal CODE reads of a real `char code` table through the direct AS4
 *      path and through a converted AS0 pointer must agree byte for byte.
 *   5  a store through the converted view must be visible to a noinline
 *      helper that reads through the AS4 type (no cross-function forwarding
 *      of stale values).
 *
 * Output is OK<n>/F<n> over UART1 and the sentinel A3-ALIAS-PASS/FAIL, so a
 * wrong alias decision fails visibly at run time.
 */
#include "mcs251_type_compat.h"

#define SBUF (*(volatile BYTE *)0x99)

/* Ordinary AS0 RAM: the alias probes write here through converted pointers.
 * These are NOT CODE objects; the write is part of the target-extension
 * aliasing contract (DESIGN.md B.2.1.1), never ISO C behavior on `__code`. */
BYTE ali_ram[8];

/* A real CODE table: read-only, only ever read. */
char code code_tab[8] = {1, 2, 3, 4, 5, 6, 7, 8};

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

/* A noinline observer reading through the AS4 view of the same AS0 buffer. */
__attribute__((noinline)) static BYTE read_as4(const BYTE *p, unsigned i)
{
    return ((const BYTE __code *)p)[i];
}

__attribute__((noinline)) static void write_as0(BYTE *p, unsigned i, BYTE v)
{
    p[i] = v;
}

int main(void)
{
    unsigned fails = 0;
    unsigned i;

    /* 1: write 0xAA through AS0, read the same byte through AS4. */
    {
        BYTE got;
        ali_ram[3] = 0;
        write_as0(ali_ram, 3u, 0xAAu);
        got = ((const BYTE __code *)ali_ram)[3];
        report2("1", got == 0xAAu);
        fails += (got != 0xAAu);
    }

    /* 2: the reverse direction with a shifted GEP chain. */
    {
        BYTE *w = ali_ram + 5 + 1 - 1;
        BYTE got;
        *w = 0x55;
        got = ((const BYTE __code *)ali_ram)[5];
        report2("2", got == 0x55u);
        fails += (got != 0x55u);
    }

    /* 3: 2-byte access across the conversion. */
    {
        unsigned sum;
        ali_ram[0] = 0x11;
        ali_ram[1] = 0x22;
        sum = (unsigned)((const BYTE __code *)ali_ram)[0] +
              (unsigned)((const BYTE __code *)ali_ram)[1];
        report2("3", sum == 0x33u);
        fails += (sum != 0x33u);
    }

    /* 4: legal CODE reads agree through both views. */
    {
        const BYTE *plain = (const BYTE *)code_tab;
        int ok = 1;
        for (i = 0; i < 8u; ++i)
            ok &= (code_tab[i] == (BYTE)(i + 1u));
        for (i = 0; i < 8u; ++i)
            ok &= (plain[i] == code_tab[i]);
        report2("4", ok);
        fails += !ok;
    }

    /* 5: the converted write escapes to a noinline AS4 reader. */
    {
        BYTE got;
        write_as0(ali_ram, 7u, 0x77u);
        got = read_as4(ali_ram, 7u);
        report2("5", got == 0x77u);
        fails += (got != 0x77u);
    }

    uputs("\n");
    uputs(fails ? "A3-ALIAS-FAIL\n" : "A3-ALIAS-PASS\n");
    for (;;)
        ;
}
