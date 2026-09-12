/*
 * mcs251-as4-cross-tu.h - shared declarations for the A2c cross-TU test.
 *
 * A2c (RUNTIME-AS-PTR-DESIGN-A.md §2.7, §3-A2c) requires the implicit-const
 * CODE type to be observed ACROSS translation units: declaration header,
 * caller TU and callee/definition TU are compiled separately, and the
 * resulting objects must agree on address space, const and signature.
 *
 * The type rules exercised here (all frozen by A2a):
 *   * `const uint8 __code *` names a pointer whose POINTEE is CODE (AS4) and
 *     implicitly const; the pointer object itself is a plain AS0 pointer.
 *   * `uint8 __code *` (without explicit const) names the same type: the
 *     implicit const is part of the CODE pointee, so both spellings must
 *     produce identical signatures.
 *   * `const uint8 *` is the generic/AS0 pointer: the declared superset
 *     relation makes an AS4 argument implicitly convertible, so a function
 *     taking it can be called with a CODE source.
 *
 * Deliberately NO reference to the linker: ordinary linkers cannot diagnose
 * invisible C type incompatibility, so this test checks the compiler-visible
 * declaration merges instead (see Inputs/mcs251-as4-cross-tu-merge.c and the
 * same-TU merge cases in mcs251-as4-cross-tu.c).
 */
#ifndef MCS251_AS4_CROSS_TU_H
#define MCS251_AS4_CROSS_TU_H

typedef unsigned char uint8;

/* Defined in the callee TU, resident in CODE (AS4), read-only. */
extern const uint8 __code shared_rom[8];

/* Callee entry points.  Both take a pointer into CODE: the pointee carries
 * AS4 plus the implicit const, the parameter object is an ordinary pointer. */
const uint8 __code *code_lookup(unsigned idx);
uint8 code_read(const uint8 __code *p, unsigned i);

/* Generic/AS0 entry point: a converted CODE source is a legal argument. */
uint8 generic_read(const uint8 *p, unsigned i);

/* Return a pointer into CODE; the caller converts it for AS0 use. */
const uint8 __code *code_base(void);

#endif /* MCS251_AS4_CROSS_TU_H */
