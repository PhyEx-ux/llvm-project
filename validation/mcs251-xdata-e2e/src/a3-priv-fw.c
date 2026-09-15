/*
 * a3-priv-fw.c - A3 private-runtime firmware (setter + global uint32 slot ABI).
 *
 * This is the SECOND runtime half of the §3-A3 supplement, and it is
 * deliberately separate from the corpus-shim firmware: each AS4 source is
 * converted to the setter's AS0 parameter with addrspacecast. The setter then
 * stores that AS0 pointer as `(uint32_t)(uintptr_t)src` in a global slot, and
 * the callee reconstructs an AS0 pointer. This second, private transport step
 * remains technical debt, not a replacement for the AS conversion.
 *
 * What this firmware proves and what it does not:
 *   * it proves the existing private-ABI path carries a converted __code
 *     source through an integer slot and the callee reads back the same bytes;
 *   * it is NOT independent proof of addrspacecast semantics: it tests the
 *     composed conversion plus private transport path. The integer-slot ABI
 *     remains approved technical debt, to migrate to standard prototypes
 *     once the v2 pointer-slot ABI is published.
 *
 * Known pre-existing constraints (not regressions from this design):
 *   * the ABI is NON-REENTRANT and ISR-interleaving corrupts the slot;
 *   * mcs251_libc.h:40 records an earlier -O2 branch-range limitation;
 *     this harness attempts both levels and must report their actual results.
 */
#include "mcs251_type_compat.h"

#define SBUF (*(volatile BYTE *)0x99)

typedef unsigned int size_t;

/* Private ABI declarations (validation/mcs251-runtime/src/mcs251_libc.h). */
void *memcpy(void *dst, const void *src, unsigned n);
char *strcpy(char *dst, const char *src);
int memcmp(const void *a, const void *b, unsigned n);
size_t strlen(const char *s);

/* CODE-resident source objects (AS4). */
char code priv_msg[] = "priv slot ABI";
BYTE code priv_pat[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};

/* A literal formed in a CODE context (AS4) and the ordinary AS0 control, so
 * this runtime's byte chain also distinguishes the two provenances. */
const char __code * const code_lit_ptr = "priv code context";
const char * const plain_lit_ptr = "priv as0 literal";

/* AS0 destinations. */
BYTE dst1[16];
char dst2[16];
BYTE cmp1[8];

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

int main(void)
{
    unsigned fails = 0;

    /* 1: memcpy_set_src + memcpy with a __code source. The call converts
     *    AS4 to AS0; the setter stores that pointer's integer value and the
     *    callee reconstructs an AS0 pointer. Bytes must arrive intact. */
    {
        memcpy(dst1, priv_pat, 8);
        int ok = 1;
        for (unsigned i = 0; i < 8; ++i)
            ok &= (dst1[i] == priv_pat[i]);
        report2("1", ok);
        fails += !ok;
    }

    /* 2: strcpy_set_src + strcpy with a __code NUL-terminated string. */
    {
        strcpy(dst2, priv_msg);
        int ok = 1;
        const char *p = priv_msg;
        for (unsigned i = 0; ; ++i) {
            ok &= (dst2[i] == p[i]);
            if (p[i] == 0)
                break;
        }
        report2("2", ok);
        fails += !ok;
    }

    /* 3: memcmp_set_src + memcmp with a __code source compared against an AS0
     *    buffer holding the same bytes (and a negative case). */
    {
        for (unsigned i = 0; i < 8; ++i)
            cmp1[i] = priv_pat[i];
        int same = memcmp(cmp1, priv_pat, 8);
        cmp1[3] ^= 0xFF;
        int diff = memcmp(cmp1, 8);
        int ok = (same == 0) && (diff != 0);
        report2("3", ok);
        fails += !ok;
    }

    /* 4: the private runtime's single-pointer strlen also accepts the __code
     *    array (this one *is* an AS4 -> AS0 argument conversion), plus the
     *    CODE-context literal vs the AS0 control literal. */
    {
        size_t n = strlen(priv_msg);
        size_t n_code = strlen(code_lit_ptr);
        size_t n_plain = strlen(plain_lit_ptr);
        int ok = (n == 13) && (n_code == 17) && (n_plain == 16) &&
                 (code_lit_ptr[0] == 'p') && (plain_lit_ptr[0] == 'p') &&
                 (code_lit_ptr[5] == 'c') && (plain_lit_ptr[5] == 'a') &&
                 (code_lit_ptr[10] == 'c') && (plain_lit_ptr[10] == 'i');
        report2("4", ok);
        fails += !ok;
    }

    uputs("\n");
    uputs(fails ? "A3-PRIV-FAIL\n" : "A3-PRIV-PASS\n");
    for (;;)
        ;
}
