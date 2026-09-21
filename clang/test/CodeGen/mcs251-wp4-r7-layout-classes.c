// WP4 revision round 7 (R3): the ThinLTO empty-invocation exemption, paired
// across the three classes that "empty in the container sense" can mean.
//
// Round 6 narrowed the exemption to six empty containers plus an empty
// module-asm string. That is still not enough in the other direction: the
// module's DataLayout is not one of those containers, and the contract check
// reads it (`getPointerSizeInBits(0)`, the program address space). An input
// carrying an explicit layout of its own -- `target datalayout = "E-p:64:64"`
// with only a triple beside it -- has declared a layout contract even with no
// functions, globals, aliases, ifuncs, named metadata or module assembly.
//
// Measured before round 7: the ordinary entry exited 1 in all four O0/O2 x
// IR/object cells ("AS0 pointer width must be 16 or 32 bits"), the ThinLTO
// entry exited 0 for IR. Round 7 adds `M.getDataLayoutStr().empty()` to the
// predicate, which separates that input from the upstream-fabricated module
// exactly (a default-constructed module never carries a layout string).
//
// The helper pins three classes on both entries where the input admits one:
//   * legal layout, otherwise empty   -> IR 0 on both entries; object 70 on
//     both entries (the object writer's pre-existing `!mcs251.signatures`
//     requirement, identical on the ordinary entry);
//   * illegal explicit layout         -> status 1 in every cell on both
//     entries, no artifact, ordinary diagnostic, and the ThinLTO IR cell is
//     required to carry the layout contract diagnostic;
//   * bare fabricated (no summary)    -> IR 0, object 70; no legal object path
//     is claimed for this input.
//
// RUN: %python %S/Inputs/mcs251-r7-layout-classes-check.py %clang_cc1

int mcs251_r7_layout_classes_marker;
