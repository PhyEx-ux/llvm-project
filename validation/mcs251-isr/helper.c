/* helper.c - ISR campaign T09 cross-TU helper object. Ordinary functions
 * only: compile-matrix.py asserts this TU carries no interrupt convention
 * and no ISR slot attribute anywhere (T09 card step 6), and that its object
 * carries no .mcs251.isr records (A3.3: ordinary objects require none).
 *
 * helper_multi exercises the existing multi-argument static parameter-slot
 * ABI (three scalar arguments); helper_ordinary is the plain cross-TU call
 * target used from an ISR body (T05: ISR-to-ordinary-helper calls keep the
 * ordinary C/Fast call path).
 */

volatile unsigned char helper_sink;

void helper_ordinary(void) {
  helper_sink = (unsigned char)(helper_sink + 1);
}

void helper_multi(unsigned char a, unsigned char b, unsigned char c) {
  helper_sink = (unsigned char)(a + b + c);
}
