// WP4 revision round 6: the ThinLTO importing entry applies the same
// two-phase MCS251 contract verdict as the ordinary code-generation entry,
// with the arithmetic phase AFTER the importing/optimization pipeline and
// BEFORE code generation.
//
// Round 5 ran the whole verdict (structural AND arithmetic) before
// runThinLTOBackend, i.e. before optimization, which rejected modules the
// ordinary entry accepts at -O2 once folding and dead-code elimination have
// run (`mul i64 %x, 0` then a truncation; an i64 product reachable only
// through a constant-false select arm). Round 6 keeps the structural phase
// before the pipeline and moves the arithmetic phase into the
// PreCodeGenModuleHook, reported through the ordinary DiagnosticsEngine exit
// (never the crash-recovery path).
//
// The helper below pins both entries against each other at O0/O2 x IR/obj:
//   * zero_mul / constant_select: rc=1 at O0 (the arithmetic check's local
//     interpreter does not fold these shapes), rc=0 at O2 with a real .ll /
//     strict ELF artifact carrying the defined symbol (legal
//     `!mcs251.signatures` included);
//   * live i64 arithmetic and a dead atomic load: rc=1 on both entries in
//     every cell, ordinary error diagnostics, no crash text;
//   * the round-6 empty-module exemption controls: module-asm-only and
//     FullDebug-CU-only modules are checked on BOTH entries now; a truly
//     empty module keeps the legal IR pass-through on both entries (its
//     object cells exit 70 on both entries through the object writer's
//     pre-existing `!mcs251.signatures` requirement, recorded as such);
//   * the upstream-fabricated empty module (bitcode without a ThinLTO
//     summary): accepted as IR, exit 70 for objects -- no legal object path
//     is claimed for this input.
//
// RUN: %python %S/Inputs/mcs251-r6-thinlto-check.py %clang_cc1
