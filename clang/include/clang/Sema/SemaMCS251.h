//===----- SemaMCS251.h - MCS-251 target-specific routines -----*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis functions specific to MCS-251.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAMCS251_H
#define LLVM_CLANG_SEMA_SEMAMCS251_H

#include "clang/AST/ASTFwd.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class Decl;
class ValueDecl;

class SemaMCS251 : public SemaBase {
public:
  SemaMCS251(Sema &S);

  bool CheckMCS251BuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);

  /// MCS-251 X1 (DESIGN.md B.1/B.2): CODE (target address space 4) is
  /// read-only, so every store through an `__code`-qualified lvalue --
  /// assignment, compound assignment, and ++/-- targets alike -- is diagnosed
  /// in Sema rather than left to the backend's fail-closed gate. \p LHS is the
  /// write target of the operation. Returns true if a diagnostic was emitted.
  bool CheckCodeStore(Expr *LHS, SourceLocation Loc);

  /// MCS-251 X1: diagnose builtin calls that store through their first
  /// (pointer) argument -- the memcpy/memmove/memset/strcpy destination
  /// family and the atomic store/exchange/RMW builtins -- when that argument
  /// points into the read-only CODE space. Returns true if a diagnostic was
  /// emitted.
  bool CheckMCS251CodeSpaceBuiltinCall(unsigned BuiltinID, CallExpr *TheCall);

  /// MCS-251 X1: a string literal that initializes a pointer into the CODE
  /// space denotes a constant object *in that space*, not in the default one
  /// (official `uint8 code *tab[] = {"a", "b"}` shape). Re-types the literal
  /// before the ordinary array-to-pointer decay so the conversion checks and
  /// CodeGen see matching address spaces. Returns true if \p RHS was
  /// adjusted.
  bool AdjustMCS251StringLiteralPointerInit(QualType LHSType, ExprResult &RHS);

  /// Enforce the DIALECT-FRONTEND-DESIGN §7.5 forced-operation restrictions on
  /// controlled fixed bit lvalues reached from the full expression \p E: the
  /// CPL toggle (`X ^= 1`, `X = !X`) is only valid as a discarded-value
  /// expression, and other self-reading read-modify-write forms (`X = ~X`,
  /// `X = X + 1`, ...) are rejected. Returns true if a diagnostic was emitted.
  bool CheckMCS251ControlledBitRMW(Expr *E, bool DiscardedValue);

  /// Recursive worker for CheckMCS251ControlledBitRMW. \p ResultUsed indicates
  /// whether the value of \p E is consumed by its parent.
  bool checkControlledBitExpr(Expr *E, bool ResultUsed);

  /// Enforce the controlled-bit rules on the *clause operands* of a completed
  /// OpenMP/OpenACC directive \p Directive. Clause operands are not full
  /// expressions for most clause kinds (only some go through
  /// ActOnFinishFullExpr), so this is checked once at directive completion.
  /// Returns true if a diagnostic was emitted.
  bool CheckMCS251ControlledBitDirectiveClauses(Stmt *Directive);

  /// P09 §6.3 N13-N15: check a call site against the frozen bit call-form
  /// exclusions. \p FnType is the callee's function type after decay;
  /// \p IsIndirect is true when the call goes through a function pointer
  /// (no resolved FunctionDecl). Emits err_mcs251_bit_call_unsupported with
  /// the frozen category ("variadic", "no-prototype" or "multi-argument
  /// indirect") and returns true when the call form is rejected.
  bool CheckMCS251BitCallForm(const FunctionType *FnType, bool IsIndirect,
                              SourceLocation Loc, SourceRange Range);

  /// G2 §4.4 (N16-N18): the frozen variadic-call gates of the B1 static-slot
  /// continuation ABI, checked at the same routing point as
  /// CheckMCS251BitCallForm (BuildResolvedCallExpr). In order:
  ///  - C1 (err_mcs251_variadic_call_indirect): any call through a function
  ///    pointer whose type contains `...`, regardless of the argument count
  ///    (the continuation slots are named after the callee symbol, so an
  ///    indirect call has no ABI);
  ///  - A (err_mcs251_variadic_call_cap_exceeded): more than the fixed six
  ///    variadic arguments -- prototype calls count \c Args.size() minus the
  ///    fixed parameters, unprototyped calls count \c Args.size() minus the
  ///    register-channel first argument;
  ///  - D1 (err_mcs251_variadic_arg_type): each argument in a variadic
  ///    position of a direct prototype call must not be in the frozen
  ///    rejection set (aggregate/union, integers wider than 32 bits,
  ///    pointers into non-ordinary address spaces; see
  ///    isMCS251RejectedVariadicType). Promotion never rescues a rejected
  ///    category, so the as-written type decides.
  /// Returns true if the call was rejected (diagnostics already emitted).
  bool CheckMCS251VariadicCall(const FunctionType *FnType, bool IsIndirect,
                               ArrayRef<Expr *> Args, SourceLocation Loc,
                               SourceRange Range);

  /// G2 §4.4.3 D2: the `__builtin_va_arg` read path enforces the same frozen
  /// rejection set as the call path (aggregate/union, integers wider than
  /// 32 bits, pointers into non-ordinary address spaces) on the target type
  /// \p T. bit stays with the existing N13/M2 boundaries (G2 §4.6: the
  /// registered va_arg-of-bit gap is not widened here). Returns true if a
  /// diagnostic was emitted.
  bool CheckMCS251VAArgType(QualType T, SourceLocation Loc,
                            SourceRange Range);

  //===--------------------------------------------------------------------===//
  // MCS251 OpenMP/OpenACC construct restriction context (P08 revision,
  // Alice ruling plan B).
  //
  // The first slice does not support MCS251 bit capabilities (bit types and
  // objects, sbit, L1 fixed-bit references) in enabled OpenMP/OpenACC
  // constructs that the frontend processes. The boundary is enforced as an
  // independent, nestable, error-recovery-safe *restriction context* entered
  // around the directive's input (pragma expression arguments, all clause
  // input, associated statement/declaration, and related Sema construction,
  // including captures and generated private copies), and checked at a finite
  // list of semantic entry points. It deliberately does not reuse the parser's
  // ParsingOpenMPDirectiveRAII/ParsingOpenACCDirectiveRAII booleans, which are
  // temporarily false while the associated body is parsed ("parsing a pragma")
  // and therefore do not describe the construct's restricted interval.
  //===--------------------------------------------------------------------===//

  /// Is any OpenMP/OpenACC construct restriction context active?
  bool inDirectiveRestriction() const { return !RestrictionFrames.empty(); }

  /// Enter one nesting level of the restriction context. \p DirectiveLoc is
  /// the pragma (directive) location reported by the note. A \p Persistent
  /// frame (declare-target/declare-variant regions) outlives the parse of its
  /// own pragma; it is left by exitPersistentDirectiveRestriction, while the
  /// parser's per-directive frames are left by exitDirectiveRestriction. The
  /// two classes interleave, and each is removed exactly by its own exit, so
  /// nesting stays balanced on every recovery path.
  void enterDirectiveRestriction(SourceLocation DirectiveLoc, bool IsOpenACC,
                                 bool Persistent = false);

  /// Leave one parser-frame nesting level (normal end, error recovery, or
  /// early return of a directive parse).
  void exitDirectiveRestriction();

  /// Leave one persistent-frame nesting level (end of a declare-target /
  /// declare-variant region).
  void exitPersistentDirectiveRestriction();

  /// Leave every nesting level. Called for an unterminated declare-target
  /// region (before later TU-end processing runs) and unconditionally at the
  /// end of the translation unit, so no parse-time restriction state can
  /// outlive the TU body.
  void drainDirectiveRestrictions();

  /// Remove the innermost restriction frame of one class from the
  /// interleaved stack and reset the per-construct diagnostic latch.
  void leaveFrame(bool Persistent);

  /// Report the first violation inside the innermost construct and point back
  /// at its pragma. At most one diagnostic is emitted per construct nesting
  /// level: the gate locates the first violation only. Returns true if this
  /// call emitted the diagnostic.
  bool diagnoseBitCapabilityInDirective(SourceLocation Loc, SourceRange Range);

  /// Finite semantic entry: a DeclRefExpr or MemberExpr is being built for \p D
  /// while a restriction context is active. Rejects references whose semantic
  /// identity carries the bit capability: a variable/field/parameter (global/
  /// static/local) whose type is or carries MCS251 bit -- including bit
  /// reached through a function-pointer/block-pointer signature (return type
  /// or parameter) or an array --, a declaration carrying the fixed-address
  /// sbit identity, or a function whose signature involves bit.
  void CheckDeclRefInDirectiveRestriction(const ValueDecl *D,
                                          SourceLocation Loc);

  /// Finite semantic entry: a declarator finished while a restriction context
  /// is active. Rejects introducing bit objects or type constructions into
  /// the construct: a bit variable, a typedef of bit (or of a signature
  /// carrying bit), or a function whose return type or parameters involve
  /// bit.
  void CheckDeclaratorInDirectiveRestriction(const Decl *D);

  /// Finite semantic entry: an sbit declaration while a restriction context
  /// is active (sbit does not go through HandleDeclarator).
  void CheckSbitDeclInDirectiveRestriction(SourceLocation Loc,
                                           SourceRange Range);

  /// Finite semantic entry: a use of the L1 fixed-bit builtin
  /// (__builtin_mcs251_bit_lvalue, identified by builtin ID) while a
  /// restriction context is active.
  void CheckBitBuiltinInDirectiveRestriction(SourceLocation Loc,
                                             SourceRange Range);

  /// Finite semantic entry: a declarative-directive argument names \p ND
  /// through directive-specific lookup (OpenMP declare target to/link/indirect
  /// lists) while a restriction context is active. Same semantic identity as
  /// an ordinary reference.
  void CheckNamedDeclArgumentInDirectiveRestriction(const NamedDecl *ND,
                                                    SourceLocation Loc);

  /// Finite semantic entry: the signature of a block literal is established
  /// while a restriction context is active. Rejects a bit return type or a
  /// bit parameter (bit reached through nested signatures included); block
  /// signatures do not pass through HandleDeclarator.
  void CheckBlockSignatureInDirectiveRestriction(QualType FnTy,
                                                 SourceLocation Loc,
                                                 SourceRange Range);

  /// Finite semantic entry: a __builtin_va_arg type argument is resolved
  /// while a restriction context is active. A bit (or bit-carrying) type
  /// argument establishes a bit value inside the construct; the
  /// outside-construct M2 gap for va_arg-of-bit does not lift the inner
  /// prohibition.
  void CheckVAArgTypeInDirectiveRestriction(QualType T, SourceLocation Loc,
                                            SourceRange Range);

  /// Finite semantic entry: a pure type input is resolved while a restriction
  /// context is active -- the type of an explicit cast or compound literal,
  /// a type-trait argument (__builtin_types_compatible_p and friends), a
  /// _Generic controlling or association type, or a sizeof/_Alignof type
  /// operand. The P08 boundary is evaluation-blind, so a type carrying the
  /// bit capability is rejected even when no value of that type is ever
  /// computed (final rework F5/F7; bit-free type inputs and every
  /// outside-construct use are unaffected).
  void CheckTypeInputInDirectiveRestriction(QualType T, SourceLocation Loc,
                                            SourceRange Range);

private:
  struct RestrictionFrame {
    SourceLocation DirectiveLoc;
    bool IsOpenACC;
    bool Persistent;
    /// Has this construct already reported its (first) violation? The latch
    /// is per frame: an inner construct reporting its own violation must not
    /// silence -- nor reopen -- the outer construct's already-spent one, so
    /// the same outer construct is never diagnosed twice even when more bit
    /// input follows a nested construct inside it.
    bool Diagnosed = false;
  };
  /// Innermost-first stack of active construct restriction contexts. Empty
  /// means no construct is being processed. The state is not serialized: PCH
  /// and cached-token/delayed inputs are checked when their semantics
  /// actually run through the entry points above, not by source range.
  llvm::SmallVector<RestrictionFrame, 4> RestrictionFrames;
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAMCS251_H
