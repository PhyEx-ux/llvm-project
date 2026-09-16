/*
 * bt14-pollute-tu2.c - BT14 pollute-RAM e2e, second TU.
 *
 * A global bit object (q1, init 1) that must land in the SAME partial-mask
 * byte as tu1's p1 (cross-TU aggregation into one record), and an
 * extern-only use of tu1's a0 (a cross-TU bit instruction).  partial()
 * reads both partial-byte bits through the bit machinery; the caller checks
 * the returned pattern against the map's declared values.
 */

__bit q1 = 1;

extern __bit a0;
extern __bit p1;

int cross_read(void) { return a0 ? 1 : 0; }

int partial(void) {
  int v = 0;
  if (p1) v |= 1;
  if (q1) v |= 2;
  return v;
}
