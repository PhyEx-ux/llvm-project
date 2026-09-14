/*
 * dpl-sentinel-caller.c - caller TU and firmware entry of the P09 P-3 DPL
 * sentinel harness (P09-BIT-CODEGEN-DESIGN section 4.3, lines 324-333).
 *
 * Real C translation unit, compiled and linked separately from the callee
 * (dpl-sentinel-callee.c); every cross-TU entry is noinline. The full DPL
 * byte is SFR 0x82 (fixed-address volatile pointer -> DIRECT addressing
 * under the compat memory contract 1,1,32,8,1 - the only SFR path).
 *
 * NOTE on shapes: the compat ABI used to reach the SFRs does not support
 * static pointer parameters, so every function in this TU takes scalars
 * only and all text is emitted character by character.
 *
 * Transcript (one line per cell, bytes in hex, checked byte-exact by the
 * host script as well):
 *   A:<pollution>:<value#>=<byte>  arg path: DPL dirtied FE/A5/FF before
 *                                  each call, value 0/1/2/-1/INT_MIN passed
 *                                  as the FIRST (bit) parameter; the callee
 *                                  must find exactly 00 (for 0) / 01 (else).
 *   R:<pollution>:<value#>=<byte>  return path: callee dirts DPL itself,
 *                                  returns a dynamic int; the caller
 *                                  captures the FULL byte right after a
 *                                  result-discarded call (no bool decode
 *                                  between the call and the capture).
 *   S/S2/S3/S4:<p>:<v>=<byte>      later-arg static slots dirted directly
 *                                  (ordinary byte stores through extern
 *                                  symbols renamed to the backend's own
 *                                  slot assembler names) with DISTINCT
 *                                  bytes per slot; each must end with
 *                                  exactly its own expected 00/01.
 *   M:<p>:<v>=<byte>               mixed signature: the position-3 bit's
 *                                  slot byte (original source numbering).
 *   C:2/3/4=<byte>                 callee-side raw slot observations from
 *                                  the final S2/S3/S4 combo.
 *   N:<p>:lo=<b>,hi=<b>            adjacent-byte sentinels around the solo
 *                                  slot survive the call unchanged.
 *   G:...                          ordinary i8/i16/i32 ABI golden values.
 *   !<tag>:<got>/<expect>          a full-byte mismatch (fail counter).
 *
 * The final line is DPL-SENTINEL-PASS or DPL-SENTINEL-FAIL(<count>). Every
 * comparison is on the FULL byte - reading DPL bit0 is never a pass
 * criterion (section 4.3 line 333), and no bit-object intrinsic is used
 * anywhere in this TU (prohibition 1).
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;

#define DPL (*(volatile u8 *)0x82)
#define SBUF (*(volatile u8 *)0x99)

extern volatile u8 cap_arg, cap_val;
extern volatile u8 slot_seen2, slot_seen3, slot_seen4;

void cap_first(__bit);
int slot_callee(__bit, __bit, __bit, __bit);
int solo_callee(__bit, __bit);
int mixed_callee(__bit, int, __bit);
__bit ret_pollute(int, int);
u8 golden_u8(int);
int golden_i32(int, int);
short golden_i16(short, short);

/* the backend's static argument slots, referenced verbatim */
extern volatile u8 solo_slot __asm__("_solo_callee_PARM_2");
extern volatile u8 slot2 __asm__("_slot_callee_PARM_2");
extern volatile u8 slot3 __asm__("_slot_callee_PARM_3");
extern volatile u8 slot4 __asm__("_slot_callee_PARM_4");
extern volatile u8 mixed_slot __asm__("_mixed_callee_PARM_3");

static volatile u16 fail;

static const int vals[5] = { 0, 1, 2, -1, -2147483647 - 1 };
static const u8 pols[3] = { 0xFE, 0xA5, 0xFF };

static void putc_(u8 c) { SBUF = c; }
static void phex(u8 v) {
  static const char hx[] = "0123456789ABCDEF";
  putc_((u8)hx[v >> 4]);
  putc_((u8)hx[v & 15]);
}
static void phex16(u16 v) {
  phex((u8)(v >> 8));
  phex((u8)v);
}
static void phex32(u32 v) {
  phex16((u16)(v >> 16));
  phex16((u16)v);
}
/* emit "<tag><p>:<v>=" - the shared line prefix of the matrix families */
static void ptag(u8 tag, u8 p, int vi) {
  putc_(tag);
  putc_(':');
  phex(p);
  putc_(':');
  putc_((u8)('0' + vi));
  putc_('=');
}

static u8 exp_byte(int v) { return v ? 1u : 0u; }
static void chk(u8 got, u8 expect, u8 tag) {
  if (got != expect) {
    fail++;
    putc_('!');
    putc_(tag);
    putc_(':');
    phex(got);
    putc_('/');
    phex(expect);
    putc_('\n');
  }
}

static void arg_matrix(void) {
  for (int pi = 0; pi < 3; pi++)
    for (int vi = 0; vi < 5; vi++) {
      DPL = pols[pi]; /* dirt the full DPL byte */
      cap_first(vals[vi]);
      ptag('A', pols[pi], vi);
      phex(cap_arg);
      putc_('\n');
      chk(cap_arg, exp_byte(vals[vi]), 'a');
      chk(cap_val, exp_byte(vals[vi]), 'b');
    }
}

static void ret_matrix(void) {
  for (int pi = 0; pi < 3; pi++)
    for (int vi = 0; vi < 5; vi++) {
      volatile u8 raw;
      ret_pollute(vals[vi], pols[pi]); /* discarded: full DPL return anyway */
      raw = DPL; /* FULL byte, captured before any bool decode */
      __bit rb = ret_pollute(vals[vi], pols[pi]); /* used: value check */
      ptag('R', pols[pi], vi);
      phex(raw);
      putc_('\n');
      chk(raw, exp_byte(vals[vi]), 'r');
      chk((u8)(rb ? 1u : 0u), exp_byte(vals[vi]), 's');
    }
}

static void slot_matrix(void) {
  /* solo: the only later slot is _PARM_2 (b at source position 2) */
  for (int pi = 0; pi < 3; pi++)
    for (int vi = 0; vi < 5; vi++) {
      solo_slot = pols[pi];
      int r = solo_callee(1, vals[vi]);
      ptag('S', pols[pi], vi);
      phex(solo_slot);
      putc_('\n');
      chk(solo_slot, exp_byte(vals[vi]), 'c');
      chk((u8)r, (u8)(1 + 2 * exp_byte(vals[vi])), 'd');
    }
  /* three slots dirted with DISTINCT bytes: each must end with exactly its
   * own expected byte (b=vals[vi], c=0, d=1) - a store smeared across slot
   * boundaries cannot pass all three checks */
  for (int pi = 0; pi < 3; pi++)
    for (int vi = 0; vi < 5; vi++) {
      slot2 = pols[pi];
      slot3 = 0xA5;
      slot4 = 0x5A;
      int r = slot_callee(1, vals[vi], 0, 1);
      ptag('2', pols[pi], vi);
      phex(slot2);
      putc_('\n');
      ptag('3', pols[pi], vi);
      phex(slot3);
      putc_('\n');
      ptag('4', pols[pi], vi);
      phex(slot4);
      putc_('\n');
      chk(slot2, exp_byte(vals[vi]), 'e');
      chk(slot3, 0x00, 'f');
      chk(slot4, 0x01, 'g');
      chk((u8)r, (u8)(9 + 2 * exp_byte(vals[vi])), 'h');
    }
  /* callee-side raw observations of the FINAL combo (FF, INT_MIN):
   * b -> 01, c -> 00, d -> 01 */
  putc_('C');
  putc_(':');
  putc_('2');
  putc_('=');
  phex(slot_seen2);
  putc_('\n');
  putc_('C');
  putc_(':');
  putc_('3');
  putc_('=');
  phex(slot_seen3);
  putc_('\n');
  putc_('C');
  putc_(':');
  putc_('4');
  putc_('=');
  phex(slot_seen4);
  putc_('\n');
  chk(slot_seen2, 0x01, 'i');
  chk(slot_seen3, 0x00, 'j');
  chk(slot_seen4, 0x01, 'k');
}

static void mixed_matrix(void) {
  /* b (position 3) rides _mixed_callee_PARM_3; x (position 2) is an int */
  for (int pi = 0; pi < 3; pi++)
    for (int vi = 0; vi < 5; vi++) {
      mixed_slot = pols[pi];
      int r = mixed_callee(1, 100, vals[vi]);
      ptag('M', pols[pi], vi);
      phex(mixed_slot);
      putc_('\n');
      chk(mixed_slot, exp_byte(vals[vi]), 'm');
      chk((u8)(r & 0xFF), (u8)((101 + (vals[vi] ? 1000 : 0)) & 0xFF), 'n');
    }
}

static void neighbor_check(void) {
  /* adjacent bytes around the solo slot keep sentinel values across the
   * call (the slot store is exactly 1 byte wide) */
  volatile u8 *p = (volatile u8 *)&solo_slot;
  for (int pi = 0; pi < 3; pi++) {
    p[-1] = 0x5A;
    p[1] = 0xA5;
    solo_slot = pols[pi];
    (void)solo_callee(0, vals[2]);
    putc_('N');
    putc_(':');
    phex(pols[pi]);
    putc_(':');
    putc_('l');
    putc_('o');
    putc_('=');
    phex(p[-1]);
    putc_(',');
    putc_('h');
    putc_('i');
    putc_('=');
    phex(p[1]);
    putc_('\n');
    chk(p[-1], 0x5A, 'x');
    chk(p[1], 0xA5, 'y');
    chk(solo_slot, 0x01, 'z');
  }
}

static void golden_abi(void) {
  static const u8 g8[6] = { 0x0B, 0x30, 0x55, 0x7A, 0x9F, 0xC4 };
  for (int i = 0; i < 6; i++) {
    u8 g = golden_u8(i);
    putc_('G');
    putc_(':');
    putc_('u');
    putc_('8');
    putc_(':');
    putc_((u8)('0' + i));
    putc_('=');
    phex(g);
    putc_('\n');
    chk(g, g8[i], 'u');
  }
  int s32 = golden_i32(0x12345678, 0x11111111);
  putc_('G');
  putc_(':');
  putc_('i');
  putc_('3');
  putc_('2');
  putc_('=');
  phex32((u32)s32);
  putc_('\n');
  chk((u8)(s32 >> 24), 0x23, 'p');
  chk((u8)(s32 >> 16), 0x45, 'q');
  chk((u8)(s32 >> 8), 0x67, 'w');
  chk((u8)s32, 0x89, 't');
  short s16 = golden_i16((short)0x1234, (short)0x5678);
  putc_('G');
  putc_(':');
  putc_('i');
  putc_('1');
  putc_('6');
  putc_('=');
  phex16((u16)s16);
  putc_('\n');
  chk((u8)(s16 >> 8), 0x68, 'v');
  chk((u8)s16, 0xAC, 'o');
}

static void emit_sentinel_prefix(void) { /* "DPL-SENTINEL-" */
  putc_('D');
  putc_('P');
  putc_('L');
  putc_('-');
  putc_('S');
  putc_('E');
  putc_('N');
  putc_('T');
  putc_('I');
  putc_('N');
  putc_('E');
  putc_('L');
  putc_('-');
}

int main(void) {
  arg_matrix();
  ret_matrix();
  slot_matrix();
  mixed_matrix();
  neighbor_check();
  golden_abi();
  emit_sentinel_prefix();
  if (fail == 0) {
    putc_('P');
    putc_('A');
    putc_('S');
    putc_('S');
    putc_('\n');
  } else {
    putc_('F');
    putc_('A');
    putc_('I');
    putc_('L');
    putc_('(');
    phex16(fail);
    putc_(')');
    putc_('\n');
  }
  for (;;) {
    fail; /* volatile read: keep the halt loop intact */
  }
}
