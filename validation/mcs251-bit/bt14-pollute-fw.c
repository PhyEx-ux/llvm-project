/*
 * bt14-pollute-fw.c - BT14 pollute-RAM e2e, firmware entry.
 *
 * Runs AFTER the bit-aware CRT applied the .mcs251.bittable records to a
 * bit window that QEMU polluted with A5/5A before reset.  main dumps the
 * raw window bytes 0x20..0x2F (the oracle read: plain volatile byte loads,
 * NOT the bit machinery), then the bit-view sums from the two TUs.  The
 * host script compares the dump against (pollution & ~mask) | value
 * computed from the link map, so a pass means:
 *   - owned bits hold their declared values (zeros included - they were
 *     cleared through the mask, not by luck of zero RAM);
 *   - neighbouring bits in a partial-mask byte keep the polluted value;
 *   - fixed-only and untouched bytes keep the polluted value untouched;
 *   - the bit instruction view agrees with the byte view.
 *
 * Transcript (checked byte-exact by bt14-pollute-e2e.sh):
 *   B<xx>=<hh>   raw IRAM byte <xx> (0x20..0x2F)
 *   L=<hh>       bit view of the full-mask byte (bits_low())
 *   P=<hh>       bit view of the partial byte (partial())
 *   X=<h>        cross-TU bit read of a0 (cross_read())
 *   F=<h>        fixed-sbit bit view of FX at 0x50 (fx_read(); byte 0x2A
 *               has no table record, so this reads the polluted value)
 *   BT14-POLLUTE-PASS  final marker (the real assertions are host-side)
 */

typedef unsigned char u8;

#define SBUF (*(volatile u8 *)0x99)

static void put(u8 c) { SBUF = c; }

static void puthex(u8 v) {
  static const u8 digits[16] = "0123456789abcdef";
  put(digits[(v >> 4) & 0xf]);
  put(digits[v & 0xf]);
}

int bits_low(void);
int partial(void);
int cross_read(void);
int fx_read(void);

int main(void) {
  volatile u8 *win = (volatile u8 *)0x20;
  u8 i;
  for (i = 0; i < 16; i++) {
    put('B');
    puthex(0x20 + i);
    put('=');
    puthex(win[i]);
    put('\n');
  }
  put('L');
  put('=');
  puthex((u8)bits_low());
  put('\n');
  put('P');
  put('=');
  puthex((u8)partial());
  put('\n');
  put('X');
  put('=');
  puthex((u8)cross_read());
  put('\n');
  put('F');
  put('=');
  puthex((u8)fx_read());
  put('\n');
  put('B'); put('T'); put('1'); put('4'); put('-');
  put('P'); put('O'); put('L'); put('L'); put('U'); put('T'); put('E');
  put('-'); put('P'); put('A'); put('S'); put('S'); put('\n');
  return 0;
}
