// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_OWNED -verify=owned %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_BIND -verify=bindobj %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_BINDFN -verify=bindfn %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_LATE -verify=late %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_ONE_LOCAL -verify=onelocal %s
// RUN: %clang_cc1 -triple mcs251 -std=c11 -emit-llvm -o /dev/null -DCASE_TWO_LOCAL -verify=twolocal %s
//
// G11-N6: an identity beyond the placement NOTE's 255-byte limit is diagnosed
// **exactly once per entity**. The length check lives in the CodeGen identity
// helper, which is entered more than once per entity (definition emission,
// declaration emission and the TU-final attribute refresh pass), so a
// regression that dropped the per-canonical-declaration deduplication would
// report the same error two or more times.
//
// This file is deliberately a `-verify` test: an extra diagnostic at a line
// whose single `<prefix>-error` directive has already been consumed is reported
// as "diagnostics seen but not expected", so every case below fails if its
// entity is diagnosed twice. Each case compiles in its own translation unit
// (selected by `-D`), because an error during external definition emission
// stops the emission of *later* external definitions in the same TU -- which
// would mask the very duplication this test is about. (Multiple over-long
// function-local statics of one TU *are* all diagnosed, which the twolocal case
// uses on purpose.)
//
// Cases:
//   owned    : an owned external definition (definition-emission path).
//   bindobj  : an unreferenced bind object (declaration path + TU-final refresh
//              pass -- a genuine multiple-entry path).
//   bindfn   : an unreferenced bind function (same, function flavour).
//   late     : placement arriving on a LATE redeclaration of an otherwise
//              ordinary object; the refresh pass re-visits the already emitted
//              global, so exactly one error for the entity, not one per
//              declaration.
//   onelocal : ONE over-long function-local static -> exactly one error; the
//              reference point for the next case.
//   twolocal : TWO independent canonical local-static entities -> exactly two
//              errors, one each. A regression that collapsed the dedup set into
//              a single TU-level boolean would emit only one and fail here,
//              while `onelocal` proves the assertion can tell 1 from 2.
//
// An external plain C entity's identity is its declaration name, so a
// 256-character identifier yields an identity of exactly 256 bytes; the
// local statics are padded so that the composed <TU>.<host>.<name> identity
// (293 bytes here) exceeds the limit as well.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define BIND(A) __attribute__((mcu_bind_at(A)))

#ifdef CASE_OWNED
int oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo PLACE(0x100) = 1; // owned-error {{MCS251 fixed placement: the generated stable symbol for 'oooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo' is 256 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
#endif

#ifdef CASE_BIND
extern int bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb BIND(0x200); // bindobj-error {{MCS251 fixed placement: the generated stable symbol for 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' is 256 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
#endif

#ifdef CASE_BINDFN
extern void nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn(void) BIND(0x300); // bindfn-error {{MCS251 fixed placement: the generated stable symbol for 'nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn' is 256 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
#endif

#ifdef CASE_LATE
// The first declaration carries no attribute; the placement arrives on
// the later redeclaration (same canonical entity).
int llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll;
int llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll PLACE(0x400) = 4; // late-error {{MCS251 fixed placement: the generated stable symbol for 'llllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllllll' is 256 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
#endif

#ifdef CASE_ONE_LOCAL
void hostf(void) {
  static int ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss PLACE(0x500) __attribute__((mcu_retain)); // onelocal-error {{MCS251 fixed placement: the generated stable symbol for 'ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss' is 293 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
}
#endif

#ifdef CASE_TWO_LOCAL
void hostf(void) {
  static int ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss PLACE(0x500) __attribute__((mcu_retain)); // twolocal-error {{MCS251 fixed placement: the generated stable symbol for 'ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss' is 293 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
  static int tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt PLACE(0x510) __attribute__((mcu_retain)); // twolocal-error {{MCS251 fixed placement: the generated stable symbol for 'tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt' is 293 bytes long, which exceeds the 255-byte limit representable in the placement note; rename or relocate the entity}}
}
#endif
