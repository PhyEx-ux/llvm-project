/*
 * dpl-sentinel-callee.c - callee TU of the P09 P-3 DPL sentinel harness
 * (P09-BIT-CODEGEN-DESIGN section 4.3, lines 324-333).
 *
 * Real C translation unit, compiled and linked separately from the caller
 * (dpl-sentinel-caller.c); every entry point is noinline. The full DPL byte
 * is SFR 0x82, reached with the same fixed-address volatile-pointer form as
 * the generated SFR headers (validation/mcs251-porting/generated/
 * stc32g-v1.h). Under the compat memory contract (1,1,32,8,1) that form
 * lowers to DIRECT addressing, the only SFR path; the QEMU run shows the
 * captures below as `mov <reg>,0x82` in the instruction trace.
 *
 * Observation points (section 4.3):
 *   - cap_first captures the FULL incoming DPL byte as the first statement
 *     of the callee body. The entry private-copy of the parameter reads DPL
 *     but never writes it, so the captured byte is exactly the byte the
 *     caller deposited at the ecall - a caller high-bits bug cannot be
 *     masked by the callee's bool decode.
 *   - slot_callee observes the RAW slot bytes `_slot_callee_PARM_2/3/4`
 *     (backend static argument slots, reached through explicit assembler
 *     names) at the top of the body. The entry decode reads those slots but
 *     never writes them, so the observation is the exact 1 byte the caller
 *     marshalled, before any bit->i1 decode consumes it.
 *   - ret_pollute dirties the full DPL byte with a caller-chosen value
 *     right before returning a dynamic int; the return marshal must
 *     overwrite the dirt with a normalized 00/01.
 *
 * Oracle independence (section 4.3 line 333 prohibitions):
 *   - this TU uses NO bit-object intrinsics (no llvm.mcs251.bit.* anywhere;
 *     the harness script greps the emitted IR to prove it);
 *   - every sentinel is observed as a FULL byte and compared against
 *     exactly 00/01 - reading DPL bit0 is never a pass criterion.
 */

typedef unsigned char u8;

#define DPL (*(volatile u8 *)0x82)

/* full incoming DPL byte and the decoded parameter value, both published
 * for the caller's transcript and assertions */
volatile u8 cap_arg;
volatile u8 cap_val;

/* raw slot bytes observed inside the callee. The assembler names are the
 * backend's own static-argument-slot symbols (`<fn>_PARM_<source pos>`);
 * the C names are renamed to them so the reference is verbatim (no extra
 * target mangling on asm names - pinned by the script's readelf checks). */
extern volatile u8 raw_slot2 __asm__("_slot_callee_PARM_2");
extern volatile u8 raw_slot3 __asm__("_slot_callee_PARM_3");
extern volatile u8 raw_slot4 __asm__("_slot_callee_PARM_4");
volatile u8 slot_seen2, slot_seen3, slot_seen4;

/* First source parameter is a bit: arrives as the full DPL byte. */
__attribute__((noinline)) void cap_first(__bit b) {
  cap_arg = DPL; /* FULL byte, before any use of the parameter value */
  cap_val = (u8)(b ? 1u : 0u);
}

/* Four bit parameters: a rides DPL, b/c/d ride _PARM_2/_PARM_3/_PARM_4
 * (ORIGINAL source positions, one byte each). */
__attribute__((noinline)) int slot_callee(__bit a, __bit b, __bit c, __bit d) {
  slot_seen2 = raw_slot2; /* raw 1B slots, before the body consumes b/c/d */
  slot_seen3 = raw_slot3;
  slot_seen4 = raw_slot4;
  return (a ? 1 : 0) + (b ? 2 : 0) + (c ? 4 : 0) + (d ? 8 : 0);
}

/* Two bit parameters: b (source position 2) rides _solo_callee_PARM_2. */
__attribute__((noinline)) int solo_callee(__bit a, __bit b) {
  return (a ? 1 : 0) + (b ? 2 : 0);
}

/* Mixed signature: a (bit) -> DPL, x (int, position 2) -> its own 4-byte
 * slot, b (bit, position 3) -> _mixed_callee_PARM_3. A "renumber slots by
 * bit ordinal" bug would move b's byte to _PARM_2; the harness pins the
 * symbol sizes/positions with readelf and the byte with a runtime
 * pollution of _PARM_3. */
__attribute__((noinline)) int mixed_callee(__bit a, int x, __bit b) {
  return (a ? 1 : 0) + x + (b ? 1000 : 0);
}

/* Dirts the full DPL byte immediately before returning a dynamic int; the
 * normalized return byte must overwrite the dirt. */
__attribute__((noinline)) __bit ret_pollute(int mode, int pollution) {
  DPL = (u8)pollution;
  return mode;
}

/* Ordinary-ABI golden (section 4.3 line 331): plain i8/i16/i32 paths keep
 * their exact values next to the bit ABI. */
__attribute__((noinline)) u8 golden_u8(int i) { return (u8)(i * 37 + 11); }
__attribute__((noinline)) int golden_i32(int a, int b) { return a + b; }
__attribute__((noinline)) short golden_i16(short a, short b) {
  return (short)(a + b);
}
