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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class Decl;
class ValueDecl;

class SemaMCS251 : public SemaBase {
public:
  SemaMCS251(Sema &S);

  bool CheckMCS251BuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall);

  //===--------------------------------------------------------------------===//
  // WP4 default correct-failure capabilities (FUNCTIONAL-GAPS-PLAN-Alice.md
  // section 3 / section 4.1). These are the C-source-level halves of the
  // unified diagnostics; the IR-layer contract check (MCS251ContractCheck,
  // mounted at pipeline start and pre-ISel) is the safety net that also
  // covers direct llc input. Each helper returns true when it rejected
  // (diagnostic already emitted) and is gated on the MCS-251 target.
  //===--------------------------------------------------------------------===//

  // --- A4/A5/A6 whole-family timing contract -------------------------------
  //
  // The rejection is a property of *performing* an atomic operation, not of
  // writing one down: an operand that is never evaluated (sizeof/_Alignof/
  // typeof), an unselected _Generic association, the unconsumed arm of
  // __builtin_choose_expr, and the untaken arm of a conditional with a
  // constant condition must all stay accepted -- the same value/selection
  // discipline CheckMCS251AbsolutePointerInit already applies to A2, and the
  // same one clang's EvaluatedExprVisitor defines for its own analyses.
  //
  // The decision is therefore taken once per full expression by
  // CheckMCS251AtomicUse, which walks the finished expression under those
  // not-evaluated rules instead of judging each construct where it happens to
  // be parsed. Judging at the point of construction cannot express this: the
  // unconsumed arm of __builtin_choose_expr and of a constant-condition `?:`
  // is built in a potentially-evaluated context and only becomes dead once
  // the enclosing selection is known. The family is recognised structurally,
  // as the IR-layer contract check does, so the two layers agree by
  // construction.
  //
  // There is exactly one choke point: the source-level checks are NOT also
  // applied at the construction sites, so a construct written in a
  // not-evaluated position is not reported twice nor rejected early. The
  // IR-layer contract check (MCS251ContractCheck) remains the safety net for
  // direct llc input and for paths that do not pass a full expression.

  /// A4/A5/A6: the single evaluation-aware decision point for the whole
  /// atomic family. Walks the full expression \p E under the language's
  /// not-evaluated rules and reports the unified diagnostic for the first
  /// atomic construct that is actually evaluated. Returns true when it
  /// rejected. Called once per full expression from ActOnFinishFullExpr.
  ///
  /// Recognised constructs, each of which is an operation when evaluated:
  ///  - AtomicExpr, the node every __c11_atomic_*/__atomic_*/__scoped_atomic_*
  ///    operation builds. The lock-free QUERIES are separate constexpr
  ///    builtins that never build one -- with MaxAtomicPromoteWidth and
  ///    MaxAtomicInlineWidth pinned to 0 they answer "not lock-free", the
  ///    honest capability answer rather than an affirmative claim;
  ///  - a call to the __sync_* family, to a fence builtin, to the runtime
  ///    lock-free query, or to one of the <stdatomic.h> operation function
  ///    names, all of which are lowered from the call itself and never reach
  ///    BuildAtomicExpr;
  ///  - a read of an _Atomic subobject (an lvalue-to-rvalue conversion whose
  ///    operand type transitively contains an _Atomic subobject), or a trivial
  ///    C++ copy/move construction from a glvalue of a type containing one (the
  ///    generated memberwise copy receives its source by reference);
  ///  - a write to an _Atomic subobject (a plain/compound assignment or an
  ///    increment/decrement whose target type contains one -- which is also
  ///    how a whole-struct assignment over an atomic member is covered).
  ///
  /// The walk skips unevaluated operands (sizeof/_Alignof/typeof/type traits/
  /// offsetof/noexcept), the unselected _Generic associations and controlling
  /// expression, the unconsumed __builtin_choose_expr arm, and the untaken arm
  /// of a conditional whose condition is an integer constant expression. A
  /// condition that merely happens to be constant-foldable (a non-const
  /// object initialized to 0) is NOT treated as a constant: both arms stay
  /// reachable, because at run time either arm can be selected.
  bool CheckMCS251AtomicUse(const Expr *E);

  /// Recursive "contains an atomic object subobject" query (memoised).
  /// True when \p T is an atomic object or contains one as a subobject, i.e.
  /// storage whose non-atomic access this target cannot honour. Pointers to
  /// atomic objects are ordinary values and are not themselves atomic
  /// storage.
  bool containsAtomicSubobject(QualType T);

private:
  /// Worker for CheckMCS251AtomicUse. Returns the first evaluated atomic
  /// construct reachable from \p E, or nullptr. \p Type is set to the
  /// offending type when the construct is an atomic-subobject access, and
  /// left null when it is an atomic operation.
  const Expr *findEvaluatedAtomicUse(const Expr *E, QualType &Type);

  /// Statement half of findEvaluatedAtomicUse. A statement expression
  /// (GNU `({ ... })`) evaluates its statements, and each of them is its own
  /// full expression -- but whether the statement expression itself is
  /// evaluated at all is decided by the enclosing expression. The inner
  /// full-expression check therefore defers while a statement-expression
  /// scope is open (see CheckMCS251AtomicUse), and the enclosing walk reaches
  /// the statements through here.
  const Expr *findEvaluatedAtomicUseInStmt(const Stmt *S, QualType &Type);

  /// True when a GNU statement expression encloses the current point, i.e.
  /// when the innermost enclosing compound scope belongs to a `({ ... })`.
  /// A block or lambda boundary ends the search: those bodies run at
  /// invocation time, not as part of the enclosing full expression.
  bool inStatementExpressionScope() const;

  /// True when the callee of \p CE is one of the operation spellings that are
  /// lowered from the call itself (__sync_*, the fences, the runtime
  /// lock-free query, the <stdatomic.h> function names) rather than through
  /// BuildAtomicExpr.
  bool isMCS251AtomicOperationCallee(const CallExpr *CE);

  /// A4/A5/A6: emit the unified family diagnostic for a NON-ATOMIC access to
  /// an object whose type transitively contains an _Atomic subobject -- an
  /// atomic member or array element has no declaration-level diagnostic,
  /// because the object declaration carrying it is a legal shape. Called by
  /// CheckMCS251AtomicUse for the access it found, and by nothing else: the
  /// decision of WHETHER the access is evaluated belongs to that walker.
  bool CheckMCS251AtomicAccess(QualType T, SourceLocation Loc);

public:

  /// A4: reject the _Atomic type qualifier/specifier in user code. The
  /// system-header exemption keeps <stdatomic.h>'s own typedefs parseable
  /// so the TU can still include the header; every actual atomic operation
  /// is rejected by CheckMCS251AtomicUse (at the end of its enclosing full
  /// expression) or by the IR layer.
  bool CheckMCS251AtomicType(QualType Underlying, SourceLocation Loc);

  /// A2: reject an integer-to-pointer cast inside a static-storage
  /// initializer (absolute-address pointer initialization).
  bool CheckMCS251AbsolutePointerInit(const Expr *Init, SourceLocation Loc);

  /// A7 (EC1): reject a call through a function pointer that passes two or
  /// more arguments (the continuation slots are named after the callee).
  bool CheckMCS251MultiArgIndirectCall(bool IsIndirect, ArrayRef<Expr *> Args,
                                       SourceLocation Loc,
                                       SourceRange Range);

  /// A8: reject weak function/variable DEFINITIONS (declarations stay
  /// accepted).
  bool CheckMCS251WeakDefinition(bool IsFunction, SourceLocation Loc,
                                 SourceRange Range);

  /// D1: reject computed goto (address-of-label and indirect goto).
  bool CheckMCS251ComputedGoto(bool IsAddrOfLabel, SourceLocation Loc);

  /// A9: reject file-scope (module-level) inline assembly.
  bool CheckMCS251FileScopeAsm(SourceLocation Loc);

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

  //===--------------------------------------------------------------------===//
  // G11 fixed placement (`mcu_place_at` / `mcu_bind_at` / `mcu_retain`),
  // G11-PLACEMENT-DESIGN.md revision 7 §2.2 / §8 (clang rows).
  //===--------------------------------------------------------------------===//

  /// Record a declaration that just received a G11 placement attribute. The
  /// redeclaration chain is not linked while attributes are processed (the
  /// ISR handling hit the same boundary), so every entity-level rule that
  /// needs other declarations, the final type, the initializer/body, or the
  /// definition status is deferred: the handlers only perform checks on the
  /// attribute arguments themselves (constant address, representability,
  /// same-declaration repeats, automatic storage, linkage) and record the
  /// declaration here for the translation-unit-final pass.
  void NoteMCS251PlacementDecl(Decl *D);

  /// Translation-unit-final G11 placement checks, called from
  /// ActOnEndOfTranslationUnit (guarded to the MCS-251 target). Per entity
  /// (canonical declaration), in this order and at most one diagnostic:
  ///  1. `mcu_place_at` + `mcu_bind_at` combination: rejected as incompatible;
  ///     no same-TU combination semantics are defined;
  ///  2. cross-declaration address conflicts (err_mcs251_place_at_conflict);
  ///  3. `mcu_bind_at` alone: no definition in this TU (an initializer, a
  ///     body, or even a tentative definition is storage), complete type,
  ///     non-zero object size (err_mcs251_bind_at_init /
  ///     err_mcs251_place_incomplete / err_mcs251_placement_zero_size);
  ///  4. `mcu_place_at`: definition in this TU, complete type, non-zero
  ///     object size, alignment of the final declaration alignment, and
  ///     overlap against the other placed entities of the TU
  ///     (err_mcs251_place_at_not_static / err_mcs251_place_incomplete /
  ///     err_mcs251_placement_zero_size / err_mcs251_place_at_alignment /
  ///     err_mcs251_place_at_overlap);
  ///  5. `mcu_retain` scope: only a placed definition may carry it;
  ///     otherwise err_mcs251_retain_requires_placement (definition without
  ///     placement) or err_mcs251_retain_no_definition (declaration only).
  void CheckMCS251PlacementEntities();

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

  /// WP4 A4: memoised "type transitively contains an _Atomic subobject" map.
  llvm::DenseMap<const Type *, bool> AtomicSubobjectCache;

  /// Declarations that received a G11 placement attribute and are waiting
  /// for the translation-unit-final pass (see NoteMCS251PlacementDecl).
  /// Deduplicated on the canonical declaration by the pass itself.
  llvm::SmallVector<Decl *, 8> PlacementDecls;
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAMCS251_H
