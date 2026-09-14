// P-1b cross-TU definition TU: the strong definition of the shared bit
// object (initializer 1, normalized), an internal static with the SAME name
// as the other TU's internal (the two must never merge), and a producer that
// exercises the discarded toggle (one CPL, no read) and a constant clear.
__bit shared_flag = 1;
static __bit hidden_a = 1;

void producer(void) {
  shared_flag ^= 1; // one CPL on the shared slot
  hidden_a = 0;     // one CLR on this TU's own slot
}
