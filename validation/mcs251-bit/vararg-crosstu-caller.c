/*
 * vararg-crosstu-caller.c - caller TU and firmware entry of the G2 B-S3
 * cross-TU variadic harness (G2-VARIADIC-DESIGN-draft.md R3, section 4.7
 * value chain + section 6 B-S3 gate 1).
 *
 * Declares _vchain with the variadic prototype ONLY (Tag 28 record role 10
 * = declaration + bit3 variadic, param_count 1) and calls it with the six
 * variadic values of the value-chain matrix:
 *
 *   (u32)'Z'       char constant widened EXPLICITLY by the cast; the
 *                  slot carries the full word 0000005A (the implicit
 *                  char->int default promotion is exercised by the
 *                  runtime printf probe, not asserted here)
 *   (u32)(u16)1000 short constant widened explicitly    -> 000003E8
 *   7u             unsigned int                         -> 00000007
 *   70000u         unsigned int crossing 16 bits        -> 00011170
 *   g + 2          char * into THIS TU's array; the callee TU
 *                  dereferences it                       -> 00000079 'y'
 *   0u             unsigned int, explicit all-zero word -> 00000000
 *
 * The harness links this TU against the callee TU (two objects) and
 * against a fused single-TU build of the same program, runs both under
 * qemu-system-mcs251, and requires the two UART transcripts to be
 * byte-identical (B-S3 gate 1 "QEMU output matches the single-TU golden").
 * The caller also owns the tail marker "PASS\n" and the data array g the
 * cross-TU pointer points into.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;

#define SBUF (*(volatile u8 __attribute__((address_space(6))) *)0x99)

char g[4] = "wxyz";

int vchain(int n, ...);

static void pass_marker(void) {
  SBUF = 'P';
  SBUF = 'A';
  SBUF = 'S';
  SBUF = 'S';
  SBUF = '\n';
}

int main(void) {
  vchain(6, (u32)'Z', (u32)(u16)1000, 7u, 70000u, g + 2, 0u);
  pass_marker();
  return 0;
}
