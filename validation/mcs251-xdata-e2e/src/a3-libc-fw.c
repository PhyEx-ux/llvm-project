/*
 * a3-libc-fw.c - A3 libc/runtime end-to-end firmware (RUNTIME-AS-PTR-DESIGN-A.md
 * §3-A3 "验收补充二").
 *
 * The approved 32-bit AS0 superset relation makes a CODE (AS4) source a legal
 * argument to a generic/AS0 parameter. This firmware exercises exactly the
 * primitives the corpus shim can actually define (single-pointer family:
 * memset/strlen/strchr/atoi) with __code sources, and reports OK<n>/F<n> over
 * UART1 so a wrong conversion or a wrong read channel fails visibly.
 *
 * The two-pointer family (memcpy/strcmp/...) and variadic functions cannot
 * exist under the current ABI (their static pointer slots are rejected), so
 * they are NOT tested here and must not be recorded as an AS-conversion
 * defect; that boundary belongs to the follow-on ABI slice.
 *
 * STRING-LITERAL PROVENANCE (Alice review R5-②): `strlen("literal in code")`
 * is NOT a CODE-context source -- the literal decays to AS0 and no
 * addrspacecast is involved.  This firmware therefore distinguishes:
 *   * `code_lit_ptr` below is initialised from a literal in a
 *     `const char __code *` context, which places the literal in AS4 and
 *     makes the call site a real AS4 -> AS0 conversion;
 *   * check 2 keeps the plain AS0 literal as the control that must stay AS0.
 *
 * Each check compares against a compile-time constant recomputed independently
 * of the function under test; check 6 now compares every byte (not a sum/xor,
 * which the optimizer can fold).
 */
#include "mcs251_type_compat.h"

#define SBUF (*(volatile BYTE *)0x99)

/* CODE-resident sources, reached through the conversion. */
char code rom_msg[] = "MCS251 code string 42";
char code rom_digits[] = "   -1234xyz";
BYTE code rom_bytes[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

/* A literal formed in a CODE context: the initialiser requires the anonymous
 * array to live in AS4, so the later call is a genuine AS4 -> AS0
 * conversion (proven in the IR check in a3-libc-e2e.sh).  The expected
 * payload in a3-libc-bytes.py includes the NUL terminator. */
const char __code * const code_lit_ptr = "code context literal";

/* The AS0 control literal: an ordinary literal passed to a generic
 * parameter.  It must stay in AS0 (never in the CODE window). */
const char * const plain_lit_ptr = "plain as0 literal";

/* AS0 destination for the write primitive. Deliberately NOT `xdata`: the
 * approved relation is AS4 -> 32-bit AS0 only, so an XDATA (AS3) destination
 * must keep being rejected -- see the negative check in a3-libc-e2e.sh. */
BYTE dstbuf[16];

/* Shim declarations (the corpus shim's single-pointer subset). */
typedef unsigned int size_t;
void *memset(void *dst, int c, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
int atoi(const char *s);

__attribute__((noinline)) static void uputc(char c) { SBUF = (BYTE)c; }

__attribute__((noinline)) static void uputs(const char *s)
{
    while (*s)
        uputc(*s++);
}

/* Report "OKn "/"Fn " without division: the digit characters are passed in
 * so the firmware needs no __divulong/__modulong runtime for / and % (the
 * division runtime is a separate deliverable and is out of this slice). */
__attribute__((noinline)) static void report2(const char *tag, int ok)
{
    uputs(ok ? "OK" : "F");
    uputs(tag);
    uputc(' ');
}

int main(void)
{
    unsigned fails = 0;
    unsigned i;

    /* 1: strlen over a __code array: AS4 -> AS0 argument conversion, then the
     *    shim's byte loop reads back through the unified channel. */
    {
        size_t n = strlen(rom_msg);
        int ok = (n == 21);
        report2("1", ok);
        fails += !ok;
    }

    /* 2: strlen over a literal formed in a CODE context (real AS4 -> AS0
     *    conversion), and the AS0 control literal (no conversion).  Both
     *    lengths are checked, so a literal wrongly placed in either space
     *    still has to read correctly through its own channel. */
    {
        size_t n_code = strlen(code_lit_ptr);
        size_t n_plain = strlen(plain_lit_ptr);
        int ok = (n_code == 20) && (n_plain == 17) &&
                 (code_lit_ptr[0] == 'c') && (plain_lit_ptr[0] == 'p');
        report2("2", ok);
        fails += !ok;
    }

    /* 3: strchr over a __code array; the returned pointer must denote the
     *    same CODE object and the character must match. */
    {
        char *p = strchr(rom_msg, '4');
        int ok = (p != 0) && (*p == '4') && ((p - (char *)rom_msg) == 19);
        report2("3", ok);
        fails += !ok;
    }

    /* 4: atoi over a __code array with leading space and a negative sign. */
    {
        int v = atoi(rom_digits);
        int ok = (v == -1234);
        report2("4", ok);
        fails += !ok;
    }

    /* 5: memset's AS0 destination stays writable (the conversion must not
     *    have made AS0 storage read-only), checked byte by byte. The
     *    destination is a plain AS0 array, so no cross-space conversion is
     *    involved on that side. */
    {
        memset(dstbuf, 0xA5, 8);
        int ok = 1;
        for (i = 0; i < 8; ++i)
            ok &= (dstbuf[i] == 0xA5);
        for (i = 8; i < 16; ++i)
            ok &= (dstbuf[i] == 0);
        report2("5", ok);
        fails += !ok;
    }

    /* 6: BYTE-BY-BYTE read of a __code array through a converted AS0 pointer
     *    (explicit local alias), compared to the compile-time pattern.  The
     *    previous version used a sum/xor, which the optimizer can fold and
     *    which cannot distinguish a byte permutation from the real payload. */
    {
        const unsigned char *p = (const unsigned char *)rom_bytes;
        int ok = 1;
        for (i = 0; i < 8; ++i)
            ok &= (p[i] == (unsigned char)(0x10u * (i + 1u)));
        report2("6", ok);
        fails += !ok;
    }

    uputs("\n");
    uputs(fails ? "A3-LIBC-FAIL\n" : "A3-LIBC-PASS\n");
    for (;;)
        ;
}
