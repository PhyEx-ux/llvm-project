// G11-N3 blocking fix (2026-09-17): a dependent-type entity has no byte size
// before instantiation, so the TU-final placement layout rules cannot run on
// it. The completeness test was NOT sufficient: Type::isIncompleteType
// documents that dependent types are never treated as incomplete, so the
// size query reached ASTContext::getTypeSizeInChars on a type whose size
// does not exist yet and crashed the compiler -- a legal input,
// `-fsyntax-only`, no instantiation needed:
//
//   template<class T> T x __attribute__((mcu_place_at(0x100)));
//
// reproduced as SIGSEGV in getTypeInfoImpl via CheckMCS251PlacementEntities.
// This profile does not implement instantiation-time placement checking, so
// a dependent-type entity carrying any placement attribute is rejected here,
// explicitly, BEFORE any layout query (fail-closed -- NOT a silent skip, and
// not a crash). Instantiation-time checking is registered as a future
// extension; template placement is not claimed to be supported.
//
// The check is a single-point predicate on the declaration's type, so it
// covers the variable-template, function-template static-local and
// class-template static-member shapes alike. De-attributed controls and a
// non-dependent placed entity (including a concrete static local in a
// function template) keep compiling.
//
// RUN: %clang_cc1 -triple mcs251 -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mcs251 -std=c++20 -fsyntax-only -verify %s
// RUN: not %clang_cc1 -triple mcs251 -std=c++17 -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=CODEGEN
// The same source on a non-MCS251 target stays on the generic
// unknown-attribute path: every mcs251 attribute is ignored with the usual
// warning and no MCS251-specific check runs (no crash, no placement
// diagnostic). This is the target-gate reverse control.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fsyntax-only -verify=nonmcs %s
//
// CODEGEN: error: fixed placement of the dependent-type entity 'vartmpl' is not supported in this profile: instantiation-time placement checking is not implemented

// Variable template: the minimal crashing reproducer (no instantiation).
template <class T>
T vartmpl __attribute__((mcu_place_at(0x100))); // expected-error {{fixed placement of the dependent-type entity 'vartmpl' is not supported in this profile: instantiation-time placement checking is not implemented}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}

// Function-template static local with a dependent type.
template <class T>
void fn() {
  static T local __attribute__((mcu_place_at(0x200))); // expected-error {{fixed placement of the dependent-type entity 'local' is not supported in this profile: instantiation-time placement checking is not implemented}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}
}

// Class-template static data member with a dependent type. (Previously the
// generic "requires a static object or function definition" wording; the
// dependent-type rejection now runs first and names the real problem. Either
// way there is a diagnostic and no crash.)
template <class T>
struct S {
  static T member __attribute__((mcu_place_at(0x300))); // expected-error {{fixed placement of the dependent-type entity 'member' is not supported in this profile: instantiation-time placement checking is not implemented}} nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}
};

// Reverse control 1: the same shapes WITHOUT a placement attribute compile.
template <class T>
T plain_vartmpl;

template <class T>
void plain_fn() {
  static T plain_local;
}

// Reverse control 2: a non-dependent placed entity stays accepted, including
// a concrete static local inside a function template (its type is known
// before instantiation, so the layout query is well-defined).
template <class T>
void concrete_fn() {
  static int concrete_local __attribute__((mcu_place_at(0x400))); // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}
}

int concrete_fixed __attribute__((mcu_place_at(0x500))); // nonmcs-warning {{unknown attribute 'mcu_place_at' ignored}}