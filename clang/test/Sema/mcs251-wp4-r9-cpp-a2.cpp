// RUN: %python %S/Inputs/mcs251-r9-a2-check.py --group cpp -- %clang_cc1
// Separate translation units pin exact syntax statuses and A2 diagnostic counts
// for template patterns/instantiations, conditional aggregates and constexpr
// constructors. IR/object rejection alone cannot establish this regression.
