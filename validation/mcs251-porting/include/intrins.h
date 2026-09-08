/*
 * Keil <intrins.h> compatibility subset for MCS251 porting v1.
 *
 * Corpus audit: `_nop_()` is the only Keil intrinsic called by the official
 * STC32G examples. Other Keil intrinsics are intentionally not declared: an
 * unsupported use must fail compilation rather than silently change timing.
 *
 * The legacy ASxxxx (.rel) path supports the inline-asm spelling directly.
 * The current ELF object streamer has no MCS251 inline-asm parser. Define
 * MCS251_PORTING_ELF_NOP_HELPER when building the ELF chain: `_nop_()` then
 * calls the supplied ELF helper, whose body is exactly NOP; ERET. This retains
 * a real NOP but adds call/return overhead; it is not cycle-identical to Keil's
 * inline intrinsic.
 */
#ifndef MCS251_PORTING_INTRINS_H
#define MCS251_PORTING_INTRINS_H

#if defined(MCS251_PORTING_ELF_NOP_HELPER)
void mcs251_porting_elf_nop(void);
#define _nop_() mcs251_porting_elf_nop()
#else
#define _nop_() __asm__ volatile("nop")
#endif

#endif /* MCS251_PORTING_INTRINS_H */
