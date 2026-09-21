// RUN: %python %S/Inputs/mcs251-r9-a2-check.py --group c -- %clang_cc1
// Defer target A2, not C's constant-initializer constraint. Check only selected,
// evaluated static compound literal objects, including address-taken objects.
// The helper keeps each rejection separate and requires exactly one diagnostic.
