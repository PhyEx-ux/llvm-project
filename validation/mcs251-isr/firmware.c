/* firmware.c - ISR campaign T09 integration firmware (ISR-TASK-BREAKDOWN.md
 * T09 card step 3). One source, eight compile-time case families, compiled by
 * compile-matrix.py at each of O0/O1/O2/O3/Os:
 *
 *   T09_CASE_GNU      GNU attribute ISR, slot 1            (external)
 *   T09_CASE_KEIL     Keil `interrupt N' suffix ISR, slot 2 (needs -fmcs251-keil)
 *   T09_CASE_INTERNAL internal unreferenced ISR, slot 3    (static, kept only
 *                      by the frontend llvm.used registration root)
 *   T09_CASE_EARLY    ISR with multiple early returns, slot 4 (calls the
 *                      multi-argument helper across TUs)
 *   T09_CASE_ARRAY    ISR with a fixed local array (volatile elements, so
 *                     the array really occupies the ISR frame at every
 *                     level), slot 5
 *   T09_CASE_SPILL    ISR with high register pressure (10 values live
 *                     across noinline call boundaries), slot 6
 *   T09_CASE_XTU      ISR calling an ordinary cross-TU helper, slot 8
 *   T09_CASE_MULTI    ISR calling the multi-argument helper through the
 *                     existing static parameter-slot ABI, slot 9
 *   T09_CASE_ALL      all of the above in one translation unit (compiled with
 *                     -fmcs251-keil; exercises several ISR definitions, and
 *                     therefore several paired metadata records, per object)
 *
 * Every case defines _main. Every case carries the same runtime-initializer
 * witnesses so every linked image supports the T10 QEMU checks:
 *   g_xinit_a = 0xA5         one-byte XINIT payload
 *   g_xinit_b = 0x1234       two-byte big-endian XINIT payload
 *   g_zero    = 0            XINIT clear-only record (zero payload)
 *   BSEG_BYTES [0x20,0x30)   cleared by the new CRT, no C variable is placed
 *                            there (no placement attribute exists in this
 *                            campaign; recorded as a QEMU expectation, not an
 *                            ELF fact)
 *
 * main() never references an interrupt entry (Sema would reject it); the
 * only helper references are ordinary calls from main and from ISR bodies,
 * which A2.11/T05 allow. Links use --area-start=DSEG=0x30 so no global can
 * alias the register-file window [0x00,0x20) or the CRT-owned bit-byte
 * window [0x20,0x30).
 */

/* ---- Runtime-initializer witnesses (all cases) ---- */
volatile unsigned char g_xinit_a = 0xA5;
volatile unsigned short g_xinit_b = 0x1234;
volatile unsigned char g_zero; /* stays zero; main guards it */
volatile unsigned char g_seed; /* written by main before any work */

/* Per-ISR activity bytes, indexed by the case slot (volatile: no level may
 * remove the ISR body stores). */
volatile unsigned char g_hit[16];

static void fail_halt(void) {
  for (;;) {
  }
}

/* ---- T09_CASE_GNU: GNU attribute spelling, external linkage ---- */
#if defined(T09_CASE_GNU) || defined(T09_CASE_ALL)
void isr_gnu(void) __attribute__((interrupt(1)));
void isr_gnu(void) {
  g_hit[1] = (unsigned char)(g_hit[1] + 1);
}
#endif

/* ---- T09_CASE_KEIL: Keil suffix spelling (-fmcs251-keil only) ---- */
#if defined(T09_CASE_KEIL) || defined(T09_CASE_ALL)
void isr_keil() interrupt 2
{
  g_hit[2] = (unsigned char)(g_hit[2] + 2);
}
#endif

/* ---- T09_CASE_INTERNAL: static, referenced by nothing but the
 * registration root; must survive every optimization level ---- */
#if defined(T09_CASE_INTERNAL) || defined(T09_CASE_ALL)
static void isr_internal(void) __attribute__((interrupt(3)));
static void isr_internal(void) {
  g_hit[3] = (unsigned char)(g_hit[3] + 3);
}
#endif

/* ---- T09_CASE_EARLY: multiple early returns around a cross-TU helper
 * call (same shape as the card-frozen mcs251-isr-opt.c body) ---- */
#if defined(T09_CASE_EARLY) || defined(T09_CASE_ALL)
extern void helper_multi(unsigned char, unsigned char, unsigned char);
void isr_early(void) __attribute__((interrupt(4)));
void isr_early(void) {
  if (g_seed == 0)
    return;
  helper_multi(g_seed, 3, 0);
  if (g_seed == 2)
    return;
  g_hit[4] = 4;
}
#endif

/* ---- T09_CASE_ARRAY: fixed-size local array in the ISR frame. The
 * elements are volatile on purpose: every access stays a real memory
 * access at every optimization level, so the eight bytes must occupy the
 * ISR stack frame. (Plain elements made the "array frame" claim false:
 * at O2 the loop folded into register arithmetic and no array survived.)
 * compile-matrix.py independently asserts the frame evidence in the
 * final assembly. */
#if defined(T09_CASE_ARRAY) || defined(T09_CASE_ALL)
void isr_array(void) __attribute__((interrupt(5)));
void isr_array(void) {
  volatile unsigned char buf[8];
  unsigned char k;
  for (k = 0; k < 8; ++k)
    buf[k] = (unsigned char)(g_seed + k);
  buf[0] = (unsigned char)(buf[0] + buf[7]);
  g_hit[5] = (unsigned char)(buf[0] + buf[3]);
}
#endif

/* ---- T09_CASE_SPILL: ten values live across noinline call boundaries
 * forces the register allocator to spill inside the ISR fixed frame.
 * spill_mix reads the volatile g_seed, so no call can be merged, hoisted
 * or constant-folded, and every result is an unknown value. Each result
 * is paired with one produced by a LATER call, so all ten stay live
 * across several call boundaries whose register mask preserves nothing
 * but spx -- keeping them in working registers is not an option at any
 * level. compile-matrix.py independently asserts the spill traffic in
 * the final assembly. */
#if defined(T09_CASE_SPILL) || defined(T09_CASE_ALL)
static unsigned char spill_mix(unsigned char x) __attribute__((noinline));
static unsigned char spill_mix(unsigned char x) {
  return (unsigned char)(x + g_seed);
}
void isr_spill(void) __attribute__((interrupt(6)));
void isr_spill(void) {
  unsigned char v0 = spill_mix(0);
  unsigned char v1 = spill_mix(1);
  unsigned char v2 = spill_mix(2);
  unsigned char v3 = spill_mix(3);
  unsigned char v4 = spill_mix(4);
  unsigned char v5 = spill_mix(5);
  unsigned char v6 = spill_mix(6);
  unsigned char v7 = spill_mix(7);
  unsigned char v8 = spill_mix(8);
  unsigned char v9 = spill_mix(9);
  g_hit[6] = (unsigned char)((unsigned char)(v0 + v9) ^
                             (unsigned char)(v1 + v8) ^
                             (unsigned char)(v2 + v7) ^
                             (unsigned char)(v3 + v6) ^
                             (unsigned char)(v4 + v5));
}
#endif

/* ---- T09_CASE_XTU: ISR calls an ordinary helper in another TU ---- */
#if defined(T09_CASE_XTU) || defined(T09_CASE_ALL)
extern void helper_ordinary(void);
void isr_xtu(void) __attribute__((interrupt(8)));
void isr_xtu(void) {
  helper_ordinary();
}
#endif

/* ---- T09_CASE_MULTI: ISR drives the existing static parameter-slot ABI
 * with three arguments; main() drives the same helper from ordinary code
 * ---- */
#if defined(T09_CASE_MULTI) || defined(T09_CASE_ALL)
extern void helper_multi(unsigned char, unsigned char, unsigned char);
void isr_multi(void) __attribute__((interrupt(9)));
void isr_multi(void) {
  helper_multi(g_seed, 1, 2);
}
#endif

int main(void) {
  g_seed = 7;

#if defined(T09_CASE_MULTI) || defined(T09_CASE_ALL)
  helper_multi(1, 2, 3);
#endif
#if defined(T09_CASE_XTU) || defined(T09_CASE_ALL)
  helper_ordinary();
#endif

  if (g_zero != 0)
    fail_halt();
  if (g_xinit_a != 0xA5)
    fail_halt();
  if (g_xinit_b != 0x1234)
    fail_halt();
  return 0;
}
