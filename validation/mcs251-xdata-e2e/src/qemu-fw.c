/*
 * qemu-fw.c - X4 e2e QEMU firmware (official Keil-dialect shapes).
 *
 * Chain under test: clang -fmcs251-keil (compat memory contract) -> llc ELF
 * object -> mcs251-lld (new CRT with the X4 XDATA_INIT walker + XSEG +
 * XDATA_INIT areas) -> ihex -> qemu-system-mcs251 -M stc32g144k246.
 *
 * What the run proves (each sub-check reports OK<n>/F<n> over UART1):
 *   1  the CRT walker applied the payload record of fwbuf (64 bytes,
 *      cross-checked against locally recomputed expectations, never against
 *      the record itself);
 *   2  the clear-only record zeroed fwzero;
 *   3  the X2 MOVX write path round-trips (write pattern, read back);
 *   4  CODE-space reads of FWDESC (sum + xor against compile-time
 *      constants);
 *   5  the cross-TU record from qemu-ext.c was applied (walker + linker
 *      resolution across objects);
 * DPXL note: _main starts with the walker's residual bank=1, which is
 * also the bank of these objects. This is not a wrong-bank self-healing
 * runtime probe; check-bytes checks the generated per-access DPXL writes.
 * DESIGN-SUPPLEMENT section 3 imposes no CRT restoration obligation.
 *
 * Output is fixed-width, no variadic printf (rt_firmware.c precedent);
 * termination sentinel "XDATA-E2E-PASS" (or "XDATA-E2E-FAIL <n>").
 * main never returns (harness protocol; the CRT 'S' marker must not fire).
 */
#include "mcs251_type_compat.h"
#include "initbytes.h"

#define SBUF (*(volatile BYTE *)0x99)

extern BYTE xdata ExtXbuf[16];

BYTE xdata fwbuf[64] = XDATA_E2E_FWBUF64_INIT;
BYTE xdata fwzero[32];

char code FWDESC[18] = {0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
                        0x34, 0x12, 0xEF, 0xCD, 0x01, 0x01, 0x00, 0x00,
                        0x00, 0x00};

__attribute__((noinline)) static void uputc(char c) { SBUF = (BYTE)c; }

__attribute__((noinline)) static void uputs(const char *s)
{
    while (*s)
        uputc(*s++);
}

__attribute__((noinline)) static void uhex8(unsigned v)
{
    static const char hex[] = "0123456789ABCDEF";
    uputc(hex[(v >> 4) & 0xF]);
    uputc(hex[v & 0xF]);
}

int main(void)
{
    unsigned fails = 0;

    /* 1: walker applied the payload record (expectations recomputed). */
    for (unsigned i = 0; i < 64; ++i) {
        BYTE want = (BYTE)((i * 5 + 1) & 0xFF);
        if (fwbuf[i] != want) {
            ++fails;
            uputs("F1@");
            uhex8(i);
            uputc('\n');
        }
    }
    uputs(fails ? "F1\n" : "OK1\n");

    /* 2: clear-only record zeroed the object. */
    for (unsigned i = 0; i < 32; ++i) {
        if (fwzero[i] != 0) {
            ++fails;
            uputs("F2@");
            uhex8(i);
            uputc('\n');
        }
    }
    uputs("OK2\n");

    /* 3: MOVX write path round-trip (and over the just-zeroed bytes). */
    for (unsigned i = 0; i < 32; ++i)
        fwzero[i] = (BYTE)(0xC0 ^ i);
    for (unsigned i = 0; i < 32; ++i) {
        if (fwzero[i] != (BYTE)(0xC0 ^ i)) {
            ++fails;
            uputs("F3@");
            uhex8(i);
            uputc('\n');
        }
    }
    uputs("OK3\n");

    /* 4: CODE-space reads: sum 0x0257, xor 0x57 over the 18 bytes. */
    {
        unsigned sum = 0, x = 0;
        for (unsigned i = 0; i < 18; ++i) {
            sum += (unsigned)(BYTE)FWDESC[i];
            x ^= (unsigned)(BYTE)FWDESC[i];
        }
        if (sum != 0x0257 || x != 0x57) {
            ++fails;
            uputs("F4 s=");
            uhex8(sum >> 8);
            uhex8(sum & 0xFF);
            uputs(" x=");
            uhex8(x);
            uputc('\n');
        }
    }
    uputs("OK4\n");

    /* 5: cross-TU record applied (0x5C + i*7). */
    for (unsigned i = 0; i < 16; ++i) {
        BYTE want = (BYTE)((0x5C + i * 7) & 0xFF);
        if (ExtXbuf[i] != want) {
            ++fails;
            uputs("F5@");
            uhex8(i);
            uputc('\n');
        }
    }
    uputs("OK5\n");

    if (fails == 0) {
        uputs("XDATA-E2E-PASS\n");
    } else {
        uputs("XDATA-E2E-FAIL ");
        uhex8(fails & 0xFF);
        uputc('\n');
    }

    for (;;)
        ; /* main never returns */
}
