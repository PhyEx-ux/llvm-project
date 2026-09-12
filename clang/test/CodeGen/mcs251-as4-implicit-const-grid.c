// RUN: %python %S/Inputs/mcs251-as4-implicit-const-grid.py --clang %clang --self-test
// RUN: %python %S/Inputs/mcs251-as4-implicit-const-grid.py --clang %clang

// A2a acceptance: the `__code` implicit-const rule is verified over the full
// three-axis cartesian product frozen by RUNTIME-AS-PTR-DESIGN-A.md §3-A2
// (A2a "验收补充") plus the three extra Alice grid groups (typedef expansion
// const level, implicit/explicit const duplication and conflicts, and the
// function-type negative).
//
// The grid lives in Inputs/mcs251-as4-implicit-const-grid.py instead of this
// file because a verify-based .c file cannot assert the *exact* canonical AST
// type of every grid point (verify only matches diagnostics).  The checker:
//
//   * enumerates the complete product 6 qualifier combinations x 15
//     declaration forms x 3 spellings = 270 cells and validates coverage
//     against the frozen axes independently of the compiler -- deleting a
//     cell, duplicating an axis tuple, or dropping an axis value is a FAIL
//     before any compiler runs;
//   * asserts per positive cell: the canonical AST type (exact match on a
//     quoted type segment, not a substring), the expected *and no other*
//     diagnostic, and the IR address space/constness of the entity;
//   * requires every negative cell to name its expected error and the reason
//     IR observation does not apply, and forbids it from skipping that
//     rationale;
//   * runs a semantic mutation probe per qualifier/pointer cell (write
//     through the pointee, assign the pointer object, assign the object) with
//     exact expectations, so the const level after typedef expansion is
//     proven and a substituted `char x;` cannot pass;
//   * `--self-test` mutates the grid and requires every mutation to be
//     caught: delete a cell, duplicate a tuple, drop an axis value, drop
//     `const`, drop `volatile`, substitute a writable `char x;`, inject an
//     unexpected discarded-qualifier warning, expect success for a negative
//     cell, remove a required probe, and sneak `__code` into an AS0 cell.
//
// Axes:
//  1. qualifier combination: __code / __code const / const __code /
//     __code volatile / __code const volatile / AS0 control
//  2. declaration form: bare object, typedef, typedef-of-typedef, pointer
//     pointee, pointer object, array, array element, function parameter,
//     function parameter array, pointer-into-CODE return, value return,
//     struct member, extern/definition merge, mismatched merge, automatic
//     storage
//  3. spelling: `__code`, -fmcs251-keil bare `code`, address_space(4)
