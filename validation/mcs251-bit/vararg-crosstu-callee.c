/*
 * vararg-crosstu-callee.c - definition TU of the G2 B-S3 cross-TU variadic
 * harness (G2-VARIADIC-DESIGN-draft.md R3, section 4.7 value chain +
 * section 6 B-S3 gate 1).
 *
 * Real C translation unit, compiled under the v2 contract (1,2,32,8,1: the
 * object carries .mcs251.attributes with Tag 28, role bit3=1 for _vchain).
 * The B1 backend emits the six 4-byte continuation slots
 * _vchain_PARM_2.._PARM_7 in THIS object; the caller TU marshals the six
 * variadic values into them before the ecall, so the slot area is the
 * cross-TU value channel this harness pins.
 *
 * va_arg consumption prints every value as EIGHT uppercase hex characters
 * (big-endian u32), so the UART transcript is pure ASCII and null-free: a
 * zero value prints "00000000", never an empty byte. The i==4 arm consumes
 * a char * and DEREFERENCES it: the pointer is formed in the caller TU
 * (points into the caller's own array g), so the dereferenced byte is only
 * correct if the 4-byte pointer value itself crossed the TU boundary
 * intact. SBUF is SFR 0x99, reached through the AS6 direct-SFR form
 * (stc32g144k246-as6.h recipe: under the v2 contract an AS0 pointer
 * constant lowers to @dr RAM access and never reaches the SFR window).
 */

#include <stdarg.h>

typedef unsigned char u8;
typedef unsigned u32;

#define SBUF (*(volatile u8 __attribute__((address_space(6))) *)0x99)

static void phex(u8 v) {
  u8 hi = (u8)(v >> 4), lo = (u8)(v & 0xfu);
  SBUF = (u8)(hi < 10 ? '0' + hi : 'A' + hi - 10);
  SBUF = (u8)(lo < 10 ? '0' + lo : 'A' + lo - 10);
}

int vchain(int n, ...) {
  va_list ap;
  int i;
  va_start(ap, n);
  for (i = 0; i < n; i++) {
    u32 v;
    if (i == 4)
      v = (u8)*va_arg(ap, char *); /* cross-TU pointer deref */
    else
      v = va_arg(ap, u32);        /* char/short promote to i32 slots */
    phex((u8)(v >> 24));
    phex((u8)(v >> 16));
    phex((u8)(v >> 8));
    phex((u8)v);
  }
  va_end(ap);
  return 0;
}
