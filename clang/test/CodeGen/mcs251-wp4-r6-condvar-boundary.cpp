// WP4 revision round 6: the condition-variable six-cell matrix and the
// explicitly registered ordinary-function-body vs StmtExpr boundary.
//
// The REJECTED half of the condition-variable rule (if/while/for/switch whose
// condition initializer contains an atomic operation, plus `__c11_atomic_init`)
// runs in clang/test/CodeGen/mcs251-wp4-r5-entry-shapes.c's helper. This test
// carries the ACCEPTED half and the boundary table:
//
//   * a plain-integer condition initializer is accepted in all six cells
//     (O0/O2 x syntax/IR/object), so the rejection is about the initializer
//     and not about the statement form;
//   * a statement expression in sizeof, or in a constant-false branch, keeps
//     its deferral;
//   * `({ while(0){ atomic_load(p); } 1; })` and the for form pass Sema
//     (syntax rc=0) but are rejected on the IR/object paths (rc=1): CodeGen
//     keeps the atomic IR node and the pre-optimization structural check
//     rejects it. A Sema acceptance is not an IR/object acceptance;
//   * the same statements in an ORDINARY function body (no statement
//     expression) are rejected by Sema in every cell. That is the registered
//     conservative analysis boundary, not a unified accept policy;
//   * a relaxed fence is accepted everywhere and is NOT interchangeable with
//     atomic_load as evidence for the boundary (the fence lowers to nothing in
//     IR, the load keeps an atomic IR node);
//   * genuinely reachable entries (goto into the branch, case of an enclosing
//     switch) are rejected in every cell, in both spellings.
//
// The verdicts above are pinned per cell by the helper with exact statuses.
//
// RUN: %python %S/Inputs/mcs251-r6-condvar-boundary-check.py %clang_cc1
