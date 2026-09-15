/*
 * bs4-probe.c - G2 B-S4 runtime probe firmware / host oracle (one source,
 * two builds).
 *
 * TARGET build (MCS251, -DMCS251_RT_TARGET is NOT set for this TU; the
 * runtime object is built with it): the probe runs as a firmware under
 * qemu-system-mcs251 and writes its transcript to SBUF (SFR 0x99, the AS6
 * direct-SFR form used by the B-S3 harness).
 *
 * HOST build (gcc): the same source is linked against the SAME
 * validation/mcs251-runtime/src/mcs251_printf.c with its host bridge
 * (#ifndef MCS251_RT_TARGET va_start/va_arg shim).  The host bridge
 * materialises the six slots from a real va_list, so every call in HOST
 * mode must pass EXACTLY six variadic arguments (fewer is undefined
 * behaviour per the shim contract in the runtime source).  The target
 * calls pass the true 0/1/2/3/5/6 counts the firmware exercises.  Both
 * transcripts are therefore produced by the same formatting engine from
 * the same values; they must be byte-identical.
 *
 * Covered (B-S4 end-to-end acceptance):
 *   - 0 / 1 / 2 / 3 / 5 / 6 variadic arguments (the frozen cap is 6);
 *   - %d %u %x %s %c %% and width/zero-pad;
 *   - f32 (%f %.2f %g) passed as promoted float through the slot;
 *   - sprintf(buf, fmt, ...) writes the CALLER buffer, 24-byte truncation
 *     with the forced NUL, and per-call position reset.
 *
 * The transcript is pure ASCII and CRLF-terminated, so it is diffable
 * byte-for-byte between the QEMU serial file and the host stdout.
 */

typedef unsigned char u8;
typedef unsigned int u32;

#ifdef BS4_HOST_ORACLE
#include <unistd.h>
#include <string.h>
#include <stdint.h>
void putchar(char c) { (void)!write(1, &c, 1); }
#define main bs4_main
/* The host bridge reads every variadic argument as printf_arg_t (uintptr_t)
 * through va_arg, so a float cannot cross it as a promoted double (it would
 * live in an SSE register).  Pass the f32 BIT PATTERN instead - which is
 * exactly what the engine consumes for %f/%g on both sides.  The target
 * build passes the real float and the backend stores the same bits. */
static unsigned f32_bits(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    return u;
}
#define F32(x) (f32_bits(x))
/* 2026-09-15 Alice review R1: the host bridge performs
 * `va_arg(ap, uintptr_t)` on every slot, so every variadic argument handed to
 * it - value, pointer, character or the zero padding - must have exactly the
 * type uintptr_t.  Passing int/unsigned/pointer/char and letting the default
 * promotions produce something else is undefined behaviour (C11 7.16.1.1p2:
 * the requested type must be compatible with the promoted actual type).  The
 * U(x) cast is therefore mandatory at every host call site, padding included.
 * The target arm passes the true counts and the backend marshals the slots,
 * so it needs no cast (and must not change). */
#define U(x) ((uintptr_t)(x))
#define P0(fmt) printf(fmt, U(0), U(0), U(0), U(0), U(0), U(0))
#define P1(fmt, a) printf(fmt, U(a), U(0), U(0), U(0), U(0), U(0))
#define P2(fmt, a, b) printf(fmt, U(a), U(b), U(0), U(0), U(0), U(0))
#define P3(fmt, a, b, c) printf(fmt, U(a), U(b), U(c), U(0), U(0), U(0))
#define P5(fmt, a, b, c, d, e) printf(fmt, U(a), U(b), U(c), U(d), U(e), U(0))
#define P6(fmt, a, b, c, d, e, f) \
    printf(fmt, U(a), U(b), U(c), U(d), U(e), U(f))
#define S1(buf, fmt, a) sprintf(buf, fmt, U(a), U(0), U(0), U(0), U(0), U(0))
#define S0(buf, fmt) sprintf(buf, fmt, U(0), U(0), U(0), U(0), U(0), U(0))
#else
#define SBUF (*(volatile u8 __attribute__((address_space(6))) *)0x99)
void putchar(char c) { SBUF = (u8)c; }
#define F32(x) (x)
#define P0(fmt) printf(fmt)
#define P1(fmt, a) printf(fmt, a)
#define P2(fmt, a, b) printf(fmt, a, b)
#define P3(fmt, a, b, c) printf(fmt, a, b, c)
#define P5(fmt, a, b, c, d, e) printf(fmt, a, b, c, d, e)
#define P6(fmt, a, b, c, d, e, f) printf(fmt, a, b, c, d, e, f)
#define S1(buf, fmt, a) sprintf(buf, fmt, a)
#define S0(buf, fmt) sprintf(buf, fmt)
#endif

/* Return type must match the runtime definition verbatim: mcs251_printf.c
 * defines `void printf(...)` / `void sprintf(...)` (private libc ABI, see the
 * runtime file header).  Declaring `int` here made the declaration and the
 * definition disagree - the host build linked anyway only because the return
 * value is unused, which is exactly the kind of latent mismatch the B-S4
 * acceptance must not carry.  2026-09-15 Alice review R1. */
void printf(const char *fmt, ...);
void sprintf(char *buf, const char *fmt, ...);

/* 32 bytes: 24 reachable output bytes + room for the sentinel check that
 * nothing is written past the truncation point. */
static char sbuf[32];

void main(void)
{
    int i;

    for (i = 0; i < 32; i++) sbuf[i] = (char)0xAA;

    P0("B-S4-PROBE\r\n");                                   /* 0 varargs */
    P0("P0\r\n");                                          /* 0 varargs */
    P1("P1:%d\r\n", 42);                                   /* 1 */
    P2("P2:%u %x\r\n", 4000000000u, 0xABCDEFu);            /* 2 */
    P3("P3:%d %u %x\r\n", -7, 4000000000u, 0xABCDEFu);     /* 3 */
    P5("P5:%d %u %x %s %c\r\n", -7, 4000000000u, 0xABCDEFu, "str", 'Z'); /* 5 */
    /* 6 variadic arguments: SIX consuming conversions (%d %u %x %s %c %d)
     * with a literal %% in the middle.  The sixth conversion comes AFTER the
     * %% on purpose: if %% consumed a slot the trailing %d would read the
     * padded/absent seventh slot instead of -6, so this single line pins both
     * "all six slots are read" and "%% consumes nothing" (2026-09-15 Alice
     * review R2: the previous line had only five consuming conversions, so
     * the sixth slot was never exercised). */
    P6("P6:%d %u %x %s %c %% %d\r\n", 1, 2, 3, "abcd", 'Z', -6); /* 6 */
    P0("PLAIN-no-conversion\r\n");
    /* 2026-09-15 Alice review R2: the '-' flag is parsed and then IGNORED by
     * the engine - %-4d right-aligns into width 4 and prints "   7", NOT the
     * C-standard left-justified "7   ".  This is a registered pre-existing
     * engine limitation (the parser advances past '-' without recording it),
     * asserted here as the actual bytes so it can never be mistaken for a
     * passing left-justification feature. */
    P3("W:%5d|%05d|%-4d|\r\n", 42, 42, 7);                 /* width forms */
    P3("F:%f %.2f %g\r\n", F32(1.5f), F32(3.25f), F32(0.5f)); /* f32 */

    /* sprintf, normal case: caller buffer receives the text */
    S1(sbuf, "%d", 12345);
    P1("S1:%s\r\n", sbuf);

    /* sprintf truncation: 36 source chars, cap 24 written, byte 23 forced
     * to NUL, nothing written past byte 23 (bytes 24..31 keep 0xAA). */
    for (i = 0; i < 32; i++) sbuf[i] = (char)0xAA;
    S0(sbuf, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    for (i = 0; i < 26; i++) {
        P2("T%02d=%02x\r\n", i, (unsigned)(unsigned char)sbuf[i]);
    }
    P1("S2:%s\r\n", sbuf);

    /* position reset: a second call restarts at buf[0] and the tail of the
     * previous, longer content must not survive the new NUL */
    S1(sbuf, "XY", 0);
    P1("S3:%s\r\n", sbuf);

    P0("DONE\r\n");
}
