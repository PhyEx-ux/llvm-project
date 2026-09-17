// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,LABEL-A
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -Werror -emit-llvm -o - -DALT_LABEL %s | FileCheck %s --check-prefixes=CHECK,LABEL-B
//
// G11-N4 §8.1/§7.1: the identity is a pure AST property. An asm label is the
// *ELF* name, never an identity input -- neither as a substitute for the
// declaration name nor as a disambiguator between two declarations.
//
// The test has two axes:
//   * within one TU, two distinct entities (A::x, B::x) whose asm labels differ
//     from their declaration names: the identity follows the declaration
//     structure (`_ZN1A1xE` / `_ZN1B1xE`), while the emitted globals keep the
//     labels (the association carrier is the ELF name);
//   * across the two RUN levels, the *same* declaration with a different asm
//     label must keep the same identity string byte-for-byte, while its ELF
//     name changes. That is the "changing a label does not change the
//     identity" direction; the opposite direction (two declarations with the
//     same label) is an ELF-level conflict and is handled by the linker's
//     record-to-symbol association checks, not by renaming the identity.
#define PLACE(A) __attribute__((mcu_place_at(A)))

namespace A { int x __asm__("slot_a") PLACE(0x100) = 1; }
namespace B { int x __asm__("slot_b") PLACE(0x110) = 2; }

#ifdef ALT_LABEL
int labelled __asm__("slot_two") PLACE(0x120) = 3;
#else
int labelled __asm__("slot_one") PLACE(0x120) = 3;
#endif

// The ELF names are the labels (the identity is not used as an ELF name).
// These are ordered before the attribute block, which lives at the end of the
// module.
// CHECK: @"\01slot_a" = global i32 1
// CHECK: @"\01slot_b" = global i32 2
// LABEL-A: @"\01slot_one" = global i32 3
// LABEL-B: @"\01slot_two" = global i32 3
// The identities are the same strings in both RUN levels (LABEL-A == LABEL-B
// on these three lines), and no asm label ever appears in an identity.
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x100,data,object,owned,0" "mcs251-stable-symbol"="_ZN1A1xE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x110,data,object,owned,0" "mcs251-stable-symbol"="_ZN1B1xE" }
// CHECK-DAG: attributes #{{[0-9]+}} = { "mcs251-place"="0x120,data,object,owned,0" "mcs251-stable-symbol"="labelled" }