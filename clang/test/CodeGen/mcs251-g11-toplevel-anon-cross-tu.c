// RUN: %python %S/Inputs/mcs251-g11-anon-cross-tu.py %clang_cc1 %t.dir
//
// G11-N4 (§8.1) anonymous-namespace ruling, cross-TU half:
//   * `A::{anonymous}::x` and `B::{anonymous}::x` in the *same* TU are
//     distinguished by the mangled component (the outer namespace is part of
//     it), not by the TU qualifier -- asserted by
//     CodeGen/mcs251-g11-toplevel-identity.cpp.
//   * the same anonymous-namespace spelling in two *different* TUs is
//     separated by the TU qualifier T = <underscored basename>.<8 hex digits
//     FNV-1a(absolute path)>, while the entity component stays byte-identical
//     across the two TUs. The identity must not fall back to the bare
//     declaration name, and it must not lean on the TU hash to separate two
//     anonymous namespaces of one TU (which the pre-revision scheme did: the
//     measured probe produced `<TU>.x` twice).
//   * reopening the same anonymous namespace yields the same entity, asserted
//     in the in-TU test.
//
// The helper compiles two generated TUs whose file names differ, and checks
// the T fields differ, the entity components match exactly, and each entity
// component is a mangled name rather than `x`.
#define PLACE(A) __attribute__((mcu_place_at(A)))
#define RETAIN __attribute__((mcu_retain))

// Unused: the RUN line only drives the helper, the fixture TUs are generated.
static int unused_anchor PLACE(0x200) RETAIN;