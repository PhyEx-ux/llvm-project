//===-- MCS251GlobalInit.h - shared global-initializer support decision ---===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WP4 shared component (FUNCTIONAL-GAPS-PLAN-Alice.md section 4.1): the
// global-initialization support decision used to live as private static
// members of the AsmPrinter. The final emitter and the early structural
// contract check (MCS251ContractCheck) must consume ONE whitelist so the two
// copies cannot drift; these free functions are that single copy.
//
// Everything here is a pure query over a Constant/Type plus the DataLayout.
// The classification entry point (classifyPointerLeaf) additionally returns
// *why* a pointer leaf is unsupported, so an early check can name the exact
// boundary (for example an inttoptr absolute-address constant) instead of a
// generic emitter policy message.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MCS251_MCS251GLOBALINIT_H
#define LLVM_LIB_TARGET_MCS251_MCS251GLOBALINIT_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/DataLayout.h"

namespace llvm {

class Constant;
class GlobalValue;
class Type;

namespace MCS251::GlobalInit {

/// Why a pointer initializer leaf is (or is not) an emittable static value.
/// The supported form (OK) is exactly: null, &global, or &global plus one
/// folded constant addend (legacy ConstantExpr Add or a GEP over a
/// GlobalVariable with all-constant indices), inside the 24-bit
/// effective-address discipline, in a 4-byte pointer container.
enum class PointerLeafKind {
  OK,               ///< null / &global[+addend]: the emittable whitelist
  AbsoluteAddress,  ///< inttoptr of an integer constant (A2 boundary)
  NonConstantGEP,   ///< GEP whose indices do not all fold to constants
  GEPOverNull,      ///< GEP whose base is not a GlobalVariable
  OffCurve,         ///< folded addend outside [-0xffffff, +0xffffff]
  UnsupportedBase,  ///< base symbol has no placed storage class (AS not 0/3/4)
  Width,            ///< pointer container is not the 4-byte 32/8 ABI container
  Other             ///< every other expression algebra (ptrtoint, casts, ...)
};

/// Classify one pointer-typed initializer leaf. On OK also returns the base
/// symbol and the folded addend. Pure query; never mutates the constant.
PointerLeafKind classifyPointerLeaf(const Constant *C, const DataLayout &DL,
                                    const GlobalValue *&Base, int64_t &Addend);

/// The historical boolean form: exactly the PointerLeafKind::OK cases.
bool isSupportedPointerLeaf(const Constant *C, const DataLayout &DL,
                            const GlobalValue *&Base, int64_t &Addend);

/// Static-storage type whitelist: byte-aligned i8/i16/i32 scalars, arrays and
/// structs of them, plus 4-byte pointer containers.
bool isSupportedMutableType(Type *Ty, const DataLayout &DL);

/// Static-storage initializer whitelist: integers, zero images and supported
/// pointer leaves, recursively over arrays and structs.
bool isSupportedMutableInitializer(const Constant *C, const DataLayout &DL);

/// Read-only (CSEG) type whitelist. \p AllowStructs widens the accepted type
/// tree to structs (the AS4-AGGREGATE slice call site).
bool isSupportedROType(Type *Ty, const DataLayout &DL, bool AllowStructs);

/// Read-only (CSEG) initializer whitelist (see MCS251AsmPrinter's RO emitters).
bool isSupportedROInitializer(const Constant *C, const DataLayout &DL,
                              bool AllowZeroImage, bool AllowStructs);

/// v1-representability walk over a global initializer (X3 placement support):
/// aggregates of emittable leaves. Keeps its own seen-set.
bool hasV1PlacementInitializer(const Constant *C, const DataLayout &DL);
bool hasV1PlacementInitializerImpl(const Constant *C, const DataLayout &DL,
                                   SmallPtrSetImpl<const Constant *> &Seen);

} // namespace MCS251::GlobalInit
} // namespace llvm

#endif // LLVM_LIB_TARGET_MCS251_MCS251GLOBALINIT_H
