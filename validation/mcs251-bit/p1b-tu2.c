// P-1b cross-TU use TU: extern-only reference to the shared object (resolves
// to the other TU's definition: one bit, no second slot), an internal static
// with the same name as the definition TU's internal (distinct object), and
// a consumer exercising the dynamic write (one write per path), the
// discarded `= !x` toggle (one CPL), and a direct condition (one sample).
extern __bit shared_flag;
static __bit hidden_a;

int consumer(int x) {
  shared_flag = x;        // SETB/CLR pair, one write per path
  hidden_a = !hidden_a;   // one CPL on this TU's own slot
  if (shared_flag)        // one sample feeding the branch
    return 1;
  return 0;
}
