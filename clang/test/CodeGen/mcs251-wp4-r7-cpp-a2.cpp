// WP4 revision round 7 (R1): the C++ A2 absolute-address pointer-initializer
// rule at the source layer, and specifically the class whose aggregate
// initializer list starts with a BASE SUBOBJECT.
//
// Round 6 taught the C walker to pair an aggregate initializer list with the
// SUBOBJECTS it initializes: an unnamed bitfield gets no element, and a class
// initializes its base subobjects BEFORE its fields ([dcl.init.aggr]), so the
// semantic element order is [base elements..., field elements...]. The C++
// base shape `struct S : B { int *p; }; S s = {{0}, (int *)0x1234};` was
// nevertheless accepted by `-fsyntax-only` while the IR and object layers
// rejected it. The cause was NOT the pairing: A2's only source call site was
// inside Sema::CheckForConstantInitializer, which C++ does not run, so EVERY
// C++ static pointer initializer was silent at the source layer. Round 7 wires
// the C++ call in Sema::CheckCompleteVariableDeclaration, gated on the
// initializer being a constant initializer.
//
// The fast source-level half (paired reject/accept with `-verify`) lives in
// clang/test/Sema/mcs251-wp4-r7-cpp-a2.cpp. This file carries the six-cell
// matrix -- O0/O2 x syntax/IR/object, exact statuses, artifact content -- and
// the artifact negative controls, all run by the helper.
//
// RUN: %python %S/Inputs/mcs251-r7-cpp-a2-check.py %clang_cc1

int mcs251_r7_cpp_a2_marker;
