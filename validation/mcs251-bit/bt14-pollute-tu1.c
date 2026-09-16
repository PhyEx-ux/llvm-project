/*
 * bt14-pollute-tu1.c - BT14 pollute-RAM e2e, definition TU.
 *
 * Declares the automatic bit objects whose power-on initialization the
 * bit-aware CRT (crt-bit.yaml) must produce under a POLLUTED bit window
 * (0xA5/0x5A written by QEMU's -device loader before reset):
 *
 *   a0..a7  pack byte 0x20 with FULL mask (a0=1, a2=1, a5=1, others 0 ->
 *           declared value 0x25; pollution there is A5, so the post-CRT
 *           byte is 0x25 only if the CRT really applied the record);
 *   p1      opens byte 0x21 with a PARTIAL mask (its neighbours must keep
 *           the polluted 0x5A pattern except the owned bits);
 *   FX      an old-style numeric sbit at fixed bit address 0x50 (byte 0x2A
 *           bit 0).  SCOPE NOTE: the C frontend inlines a numeric sbit into
 *           absolute bit-address instructions and emits no kind-2 .mcs251.bit
 *           record, so byte 0x2A carries no lld owner row and no table
 *           record; "the CRT must never touch it" is therefore exercised as
 *           "a byte only ever touched by a fixed sbit keeps its polluted
 *           power-on value".  (The kind-2 owner-fixed map case is covered at
 *           lit level by bit-profile.test/fixed.yaml.)
 *
 * bits_low() reads the full-mask byte back through the BIT machinery (the
 * real bit instructions), so the transcript proves the bit view equals the
 * byte view of the initialized window.  fx_read() reads FX the same way.
 */

__bit a0 = 1;
__bit a1 = 0;
__bit a2 = 1;
__bit a3 = 0;
__bit a4 = 0;
__bit a5 = 1;
__bit a6 = 0;
__bit a7 = 0;
__bit p1 = 1;

/* Old-style sbit at a fixed bit address: bit 0x50 = byte 0x2A, bit 0. */
sbit FX = 0x50;

int bits_low(void) {
  int v = 0;
  if (a0) v |= 1;
  if (a1) v |= 2;
  if (a2) v |= 4;
  if (a3) v |= 8;
  if (a4) v |= 16;
  if (a5) v |= 32;
  if (a6) v |= 64;
  if (a7) v |= 128;
  return v;
}

int fx_read(void) { return FX ? 1 : 0; }
