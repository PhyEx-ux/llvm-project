//===------ SemaMCS251.cpp - MCS-251 target-specific routines -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to MCS-251.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaMCS251.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OpenACCClause.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include <optional>

namespace clang {

SemaMCS251::SemaMCS251(Sema &S) : SemaBase(S) {}

/// Strip the expressions that are transparent for the purpose of identifying a
/// controlled fixed bit reference: parentheses, implicit casts, and a resolved
/// `_Generic` selection or `__builtin_choose_expr` (which denote their selected
/// sub-expression). These forward identity, so `_Generic(0, int: X)` and
/// `__builtin_choose_expr(1, X, Y)` refer to the same fixed bit as `X`/`Y`.
static const Expr *stripToFixedBitDenotation(const Expr *E) {
  while (E) {
    if (isa<ParenExpr>(E) || isa<ImplicitCastExpr>(E)) {
      E = cast<Expr>(*E->child_begin());
      continue;
    }
    if (auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
      if (GSE->isResultDependent())
        return E;
      E = GSE->getResultExpr();
      continue;
    }
    if (auto *CE = dyn_cast<ChooseExpr>(E)) {
      E = CE->getChosenSubExpr();
      continue;
    }
    break;
  }
  return E;
}

/// Return the fixed bit address denoted by \p E if \p E is a controlled
/// MCS-251 fixed bit reference: either a reference to an old-style `sbit`
/// declaration (which carries an MCS251BitAddress attribute) or a call to
/// __builtin_mcs251_bit_lvalue. Returns std::nullopt for anything else.
///
/// The builtin call is validated strictly (builtin ID and exactly one
/// argument) before its operand is inspected: no other CallExpr may be mistaken
/// for a fixed reference, which also avoids touching getArg(0) on a call that
/// has no arguments.
static std::optional<llvm::APSInt>
getMCS251FixedBitAddress(const Expr *E, ASTContext &Ctx) {
  if (!E)
    return std::nullopt;
  E = stripToFixedBitDenotation(E->IgnoreParenImpCasts());
  if (!E)
    return std::nullopt;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD)
      return std::nullopt;
    const auto *Attr = VD->getAttr<MCS251BitAddressAttr>();
    if (!Attr)
      return std::nullopt;
    return Attr->getAddress()->getIntegerConstantExpr(Ctx);
  }
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD || FD->getBuiltinID() != MCS251::BI__builtin_mcs251_bit_lvalue)
      return std::nullopt;
    if (CE->getNumArgs() != 1)
      return std::nullopt;
    return CE->getArg(0)->getIntegerConstantExpr(Ctx);
  }
  return std::nullopt;
}

/// Is \p E a controlled MCS-251 fixed bit lvalue?
static bool isMCS251ControlledBitLValue(const Expr *E, ASTContext &Ctx) {
  return getMCS251FixedBitAddress(E, Ctx).has_value();
}

/// Do \p A and \p B denote the same fixed bit location? Identity is by
/// resolved constant bit address, so a `sbit` at 0x24 aliases the builtin at
/// 0x24, and two distinct `sbit` names bound to the same address alias too.
static bool isSameMCS251FixedBit(const Expr *A, const Expr *B,
                                 ASTContext &Ctx) {
  std::optional<llvm::APSInt> AA = getMCS251FixedBitAddress(A, Ctx);
  std::optional<llvm::APSInt> AB = getMCS251FixedBitAddress(B, Ctx);
  return AA && AB && *AA == *AB;
}

/// Is \p E the integer constant 1 (the only RHS for which `X ^= RHS` is the
/// §7.5 CPL toggle)?
///
/// The comparison is done on the full APSInt, before any narrowing: a wide
/// value whose low bits happen to be 1 (e.g. `((unsigned __int128)1 << 64) | 1`)
/// has an active width above 1 and is not the constant 1.
static bool isConstantOne(const Expr *E, ASTContext &Ctx) {
  std::optional<llvm::APSInt> V =
      E->IgnoreParenImpCasts()->getIntegerConstantExpr(Ctx);
  if (!V)
    return false;
  if (V->isSigned() && V->isNegative())
    return false;
  // Reject anything wider than a single bit before narrowing to 1.
  if (V->getActiveBits() > 1)
    return false;
  return V->getZExtValue() == 1;
}

/// If \p E folds to an integer constant, return its boolean truth value.
/// Used by the dead-branch folding shared by every scanner: a short-circuited
/// `&&`/`||` RHS, the unselected branch of a folded `?:` or GNU `c ?: x`, and
/// the dead branch of an `if (0)`/`while (0)` are never evaluated, so they can
/// neither read the bit nor create a bit object. The condition itself has
/// always been scanned first, so a (possibly folded) expression that still
/// reads the bit is not lost.
static std::optional<bool> getFoldedCondition(const Expr *E, ASTContext &Ctx) {
  if (std::optional<llvm::APSInt> V =
          E->IgnoreParenImpCasts()->getIntegerConstantExpr(Ctx))
    return !V->isZero();
  return std::nullopt;
}

bool SemaMCS251::CheckMCS251BuiltinFunctionCall(unsigned BuiltinID,
                                                CallExpr *TheCall) {
  ASTContext &Context = getASTContext();
  switch (BuiltinID) {
  case MCS251::BI__builtin_mcs251_bit_lvalue: {
    // P08 revision (plan B): the L1 fixed-bit builtin is rejected by builtin
    // ID anywhere inside an enabled OpenMP/OpenACC construct, before the
    // ordinary argument checking below runs.
    if (inDirectiveRestriction()) {
      CheckBitBuiltinInDirectiveRestriction(TheCall->getBeginLoc(),
                                            TheCall->getSourceRange());
      return true;
    }

    // P08: the bit dialect is C-only for the first slice. The builtin is
    // registered at the target level (independent of keyword registration), so
    // it must be rejected explicitly in C++ rather than silently accepted.
    if (getLangOpts().CPlusPlus) {
      Diag(TheCall->getBeginLoc(), diag::err_mcs251_bit_cxx_unsupported)
          << "__builtin_mcs251_bit_lvalue" << TheCall->getSourceRange();
      return true;
    }

    if (SemaRef.checkArgCount(TheCall, 1))
      return true;

    Expr *Arg = TheCall->getArg(0);
    // The builtin uses CustomTypeChecking, so the argument reaches us with no
    // implicit conversion applied: the integer constant expression is evaluated
    // at its true (possibly widened) type. Check the full APSInt before any
    // narrowing so over-wide and negative values are rejected on their real
    // width. Address 0 is legal.
    //
    // Limitation: a signed constant expression that overflowed during folding
    // has already been wrapped before it reaches here, so an overflowing
    // expression such as `2147483647 + 1` is evaluated as the wrapped value and
    // is not distinguishable from the literal. That is a known limit of the
    // constant-expression evaluator, not a claimed hard rejection.
    std::optional<llvm::APSInt> Value = Arg->getIntegerConstantExpr(Context);
    if (!Value) {
      Diag(Arg->getBeginLoc(), diag::err_mcs251_bit_lvalue_not_constant)
          << "__builtin_mcs251_bit_lvalue" << Arg->getSourceRange();
      return true;
    }
    if ((Value->isSigned() && Value->isNegative()) ||
        Value->getActiveBits() > 8 || Value->getZExtValue() > 0xFF) {
      SmallString<24> Buf;
      Value->toString(Buf, 10);
      Diag(Arg->getBeginLoc(), diag::err_mcs251_bit_lvalue_range)
          << Buf << Arg->getSourceRange();
      return true;
    }

    // The result is a controlled, volatile, non-addressable bit lvalue (BT04
    // §7.2). The value semantics are boolean; the fixed bit address is carried
    // in the single operand. It is not an ordinary function call result and it
    // does not yield a pointer or byte object. CodeGen for the closed
    // capability is M2; until then the frontend fails closed.
    TheCall->setType(Context.getVolatileType(Context.MCS251BitTy));
    TheCall->setValueKind(VK_LValue);
    return false;
  }
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// Evaluation-aware traversal
//===----------------------------------------------------------------------===//

/// How a sub-expression's value is used by its parent. Unevaluated operands
/// (sizeof operand, unselected `_Generic`/`choose` association) are handled by
/// the callers returning early rather than by a value here.
enum class UseKind {
  /// The operand is evaluated but its value is discarded.
  Discarded,
  /// The operand is evaluated and its value is consumed.
  Used,
};

namespace {

/// Walks an expression/statement tree enforcing the DIALECT-FRONTEND-DESIGN
/// §7.5 controlled fixed bit operation rules. It is evaluation-aware: which
/// sub-expressions are evaluated at all, and whether their values are consumed,
/// determines whether a CPL toggle is legal and whether a self-read RMW exists.
struct ControlledBitChecker {
  SemaMCS251 &Self;
  ASTContext &Ctx;

  ControlledBitChecker(SemaMCS251 &S) : Self(S), Ctx(S.getASTContext()) {}

  bool checkExpr(const Expr *E, UseKind Use);
  bool checkStmt(const Stmt *S, UseKind Use);
  bool checkCompound(const CompoundStmt *C, UseKind LastUse);
  bool checkDeclStmt(const DeclStmt *DS);
  bool checkAssignment(const BinaryOperator *BO, UseKind Use);
  bool checkDirectiveClauseOperands(const Stmt *S);

  const Expr *findRead(const Expr *E, const Expr *LHS);
  const Expr *findReadAddressComputation(const Expr *E, const Expr *LHS);
  const Expr *findReadInStmt(const Stmt *S, const Expr *LHS);
};

} // namespace

/// The assignment whose LHS is a controlled fixed bit lvalue, or nullptr.
static const BinaryOperator *asFixedBitAssignment(const Expr *E,
                                                  ASTContext &Ctx) {
  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || !BO->isAssignmentOp())
    return nullptr;
  if (!isMCS251ControlledBitLValue(BO->getLHS(), Ctx))
    return nullptr;
  return BO;
}

/// The iterator-modifier expression of the locator-list/mapping clauses that
/// store it *outside* children(): depend (OMPDependClause::getModifier,
/// stored after the varlist), and map/to/from
/// (getIteratorModifier, stored at trailing slot 2*varlist_size()). It is an
/// OMPIteratorExpr whose begin/end/step ranges are real runtime evaluations
/// that expand the locator list, so a children()-based traversal never
/// reaching the slot silently drops them. Returns nullptr for every other
/// clause kind and for clauses without an iterator modifier.
static const Expr *getOMPIteratorModifier(const OMPClause *C) {
  switch (C->getClauseKind()) {
  case llvm::omp::OMPC_depend:
    return cast<OMPDependClause>(C)->getModifier();
  case llvm::omp::OMPC_map:
    return const_cast<OMPMapClause *>(cast<OMPMapClause>(C))
        ->getIteratorModifier();
  case llvm::omp::OMPC_to:
    return cast<OMPToClause>(C)->getIteratorModifier();
  case llvm::omp::OMPC_from:
    return cast<OMPFromClause>(C)->getIteratorModifier();
  default:
    return nullptr;
  }
}

/// Is \p E a call to a builtin whose arguments are unevaluated (for example
/// `__builtin_constant_p`, `__builtin_classify_type`)? The builtin metadata
/// carries this as the `UnevaluatedArguments` attribute (see Builtins.td), so
/// the operand is not a runtime evaluation and must not be checked.
///
/// Exception: `__builtin_dynamic_object_size` also carries the attribute but
/// is *not* treated as unevaluated here. Unlike `__builtin_object_size`, whose
/// result is always a compile-time constant, the dynamic form may lower to a
/// runtime objectsize computation that consumes its argument pointer. For the
/// controlled-bit read rules its argument is therefore evaluated: reads,
/// complex RMWs, and used toggles inside it are diagnosed (a discarded
/// `X ^= 1` remains allowed). Both forms still count as unevaluated for the
/// bit-object rule in CodeGen's constant guard, which tracks object creation
/// rather than value reads.
static bool isUnevaluatedBuiltinCall(const Expr *E, ASTContext &Ctx) {
  const auto *CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return false;
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return false;
  unsigned ID = FD->getBuiltinID();
  if (ID == 0)
    return false;
  if (ID == Builtin::BI__builtin_dynamic_object_size)
    return false;
  return Ctx.BuiltinInfo.isUnevaluated(ID);
}

bool ControlledBitChecker::checkAssignment(const BinaryOperator *BO,
                                           UseKind Use) {
  Expr *LHS = BO->getLHS();
  if (BO->isCompoundAssignmentOp()) {
    // Only `X ^= 1` is the §7.5 CPL toggle. Any other compound assignment
    // (X ^= 0, X ^= 2, X ^= y, X |= 1, ...) reads the bit for an ordinary
    // computation and is a rejected complex RMW regardless of result usage.
    // The RHS must be exactly the constant 1, compared at full precision.
    if (BO->getOpcode() != BO_XorAssign ||
        !isConstantOne(BO->getRHS(), Ctx)) {
      Self.Diag(BO->getOperatorLoc(), diag::err_mcs251_bit_rmw_unsupported)
          << BO->getOpcodeStr() << BO->getSourceRange();
      return true;
    }
    // `X ^= 1` is the CPL toggle: valid only where its value is discarded.
    if (Use == UseKind::Used) {
      Self.Diag(BO->getOperatorLoc(), diag::err_mcs251_bit_lvalue_result_used)
          << BO->getSourceRange();
      return true;
    }
    return false;
  }

  // Plain `X = RHS`.
  const Expr *RHS = BO->getRHS();
  // `X = !X` (same fixed bit) is the allowed CPL toggle only where discarded.
  if (const auto *UO = dyn_cast<UnaryOperator>(RHS->IgnoreParenImpCasts());
      UO && UO->getOpcode() == UO_LNot &&
      isSameMCS251FixedBit(UO->getSubExpr(), LHS, Ctx)) {
    if (Use == UseKind::Used) {
      Self.Diag(BO->getOperatorLoc(), diag::err_mcs251_bit_lvalue_result_used)
          << BO->getSourceRange();
      return true;
    }
    return false;
  }
  // Any other self-read (notably `X = ~X`, `X = X + 1`) is a rejected complex
  // RMW even when discarded. A pure write such as `X = 0`, or a nested
  // statement that only writes X, is not a read. Reads inside unevaluated
  // operands do not count.
  if (const Expr *Read = findRead(RHS, LHS)) {
    Self.Diag(Read->getExprLoc(), diag::err_mcs251_bit_rmw_unsupported)
        << BO->getOpcodeStr() << Read->getSourceRange();
    return true;
  }
  // The RHS is still an evaluated expression in its own right (it may contain a
  // consumed toggle, e.g. `Y = ({ X ^= 1; })`); check it with its value used.
  return checkExpr(RHS, UseKind::Used);
}

bool ControlledBitChecker::checkExpr(const Expr *E, UseKind Use) {
  if (!E)
    return false;

  // A controlled fixed bit lvalue used as an inc/dec target is a self-reading
  // RMW.
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if ((UO->getOpcode() == UO_PreInc || UO->getOpcode() == UO_PostInc ||
         UO->getOpcode() == UO_PreDec || UO->getOpcode() == UO_PostDec) &&
        isMCS251ControlledBitLValue(UO->getSubExpr(), Ctx)) {
      Self.Diag(UO->getOperatorLoc(), diag::err_mcs251_bit_rmw_unsupported)
          << (UO->isIncrementOp() ? "++" : "--") << UO->getSourceRange();
      return true;
    }
  }

  // The node itself as an assignment target.
  if (const auto *BO = asFixedBitAssignment(E, Ctx))
    return checkAssignment(BO, Use);

  // _Generic: the controlling expression is not evaluated; only the selected
  // association is evaluated, with this node's result usage.
  if (const auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
    if (GSE->isResultDependent()) {
      // Cannot resolve the selection: conservatively check every association.
      for (const Expr *Assoc : GSE->getAssocExprs())
        if (checkExpr(Assoc, Use))
          return true;
      return false;
    }
    return checkExpr(GSE->getResultExpr(), Use);
  }

  // __builtin_choose_expr: only the chosen branch is evaluated.
  if (const auto *CE = dyn_cast<ChooseExpr>(E))
    return checkExpr(CE->getChosenSubExpr(), Use);

  // Statement expression: every statement is evaluated; the last expression
  // statement's value flows out with this node's usage, earlier ones discard.
  if (const auto *SE = dyn_cast<StmtExpr>(E))
    return checkCompound(SE->getSubStmt(), Use);

  // sizeof/alignof and friends are unevaluated.
  if (isa<UnaryExprOrTypeTraitExpr>(E))
    return false;

  // A builtin whose arguments are unevaluated (__builtin_constant_p,
  // __builtin_classify_type, ...) does not evaluate its operand at runtime.
  if (isUnevaluatedBuiltinCall(E, Ctx))
    return false;

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_Comma)
      return checkExpr(BO->getLHS(), UseKind::Discarded) ||
             checkExpr(BO->getRHS(), Use);
    // Short-circuit: the LHS is always evaluated; the RHS is evaluated only
    // when a constant LHS does not already decide the result.
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr) {
      if (checkExpr(BO->getLHS(), UseKind::Used))
        return true;
      if (std::optional<bool> LTrue = getFoldedCondition(BO->getLHS(), Ctx)) {
        bool ShortCircuits = (BO->getOpcode() == BO_LAnd) ? !*LTrue : *LTrue;
        if (ShortCircuits)
          return false;
      }
      return checkExpr(BO->getRHS(), UseKind::Used);
    }
    return checkExpr(BO->getLHS(), UseKind::Used) ||
           checkExpr(BO->getRHS(), UseKind::Used);
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    if (checkExpr(CO->getCond(), UseKind::Used))
      return true;
    // A constant condition makes the unselected branch dead code.
    if (std::optional<bool> CTrue = getFoldedCondition(CO->getCond(), Ctx))
      return checkExpr(*CTrue ? CO->getTrueExpr() : CO->getFalseExpr(), Use);
    return checkExpr(CO->getTrueExpr(), Use) ||
           checkExpr(CO->getFalseExpr(), Use);
  }

  // GNU `c ?: x`: the common expression is evaluated and used; when it folds
  // to non-zero the false expression is dead code.
  if (const auto *BCO = dyn_cast<BinaryConditionalOperator>(E)) {
    if (checkExpr(BCO->getCommon(), UseKind::Used))
      return true;
    if (std::optional<bool> CTrue = getFoldedCondition(BCO->getCommon(), Ctx);
        CTrue && *CTrue)
      return false;
    return checkExpr(BCO->getFalseExpr(), Use);
  }

  if (const auto *CE = dyn_cast<CStyleCastExpr>(E))
    return checkExpr(CE->getSubExpr(), CE->getType()->isVoidType()
                                             ? UseKind::Discarded
                                             : Use);
  if (const auto *PE = dyn_cast<ParenExpr>(E))
    return checkExpr(PE->getSubExpr(), Use);
  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    return checkExpr(ICE->getSubExpr(), ICE->getType()->isVoidType()
                                             ? UseKind::Discarded
                                             : Use);

  // Generic fallback: every evaluated child value is consumed by this node
  // (call arguments, subscript/member bases, unary operands, ...). This catches
  // `g(X ^= 1)` and `p[X ^= 1]`.
  for (const Stmt *Child : E->children())
    if (const Expr *Sub = dyn_cast_or_null<Expr>(Child))
      if (checkExpr(Sub, UseKind::Used))
        return true;
  return false;
}

bool ControlledBitChecker::checkCompound(const CompoundStmt *C, UseKind LastUse) {
  const Stmt *const *Stmts = C->body_begin();
  unsigned N = C->size();
  for (unsigned I = 0; I != N; ++I) {
    UseKind Use = (I + 1 == N) ? LastUse : UseKind::Discarded;
    if (checkStmt(Stmts[I], Use))
      return true;
  }
  return false;
}

bool ControlledBitChecker::checkDeclStmt(const DeclStmt *DS) {
  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      continue;
    // Initializers are evaluated and consumed (the value is stored).
    if (const Expr *Init = VD->getInit())
      if (checkExpr(Init, UseKind::Used))
        return true;
  }
  return false;
}

bool ControlledBitChecker::checkStmt(const Stmt *S, UseKind Use) {
  if (!S)
    return false;

  // An expression statement is represented directly as an Expr node in the
  // statement list.
  if (const auto *E = dyn_cast<Expr>(S))
    return checkExpr(E, Use);

  if (const auto *DS = dyn_cast<DeclStmt>(S))
    return checkDeclStmt(DS);

  if (const auto *C = dyn_cast<CompoundStmt>(S))
    return checkCompound(C, Use);

  if (const auto *IS = dyn_cast<IfStmt>(S)) {
    // The init-statement (C23) and condition are evaluated; the condition value
    // is consumed by the branch decision. An if statement has no value, so both
    // branches evaluate their statements with the value discarded.
    if (const Stmt *Init = IS->getInit())
      if (checkStmt(Init, UseKind::Discarded))
        return true;
    std::optional<bool> CondTrue;
    if (const Expr *Cond = IS->getCond()) {
      if (checkExpr(Cond, UseKind::Used))
        return true;
      CondTrue = getFoldedCondition(Cond, Ctx);
    }
    if (CondTrue) {
      // A constant condition makes the other branch dead code.
      const Stmt *Live = *CondTrue ? IS->getThen() : IS->getElse();
      return Live && checkStmt(Live, UseKind::Discarded);
    }
    if (checkStmt(IS->getThen(), UseKind::Discarded))
      return true;
    if (const Stmt *Else = IS->getElse())
      if (checkStmt(Else, UseKind::Discarded))
        return true;
    return false;
  }

  if (const auto *WS = dyn_cast<WhileStmt>(S)) {
    if (const Expr *Cond = WS->getCond()) {
      if (checkExpr(Cond, UseKind::Used))
        return true;
      // A constant-false condition means the body never executes.
      if (std::optional<bool> CTrue = getFoldedCondition(Cond, Ctx);
          CTrue && !*CTrue)
        return false;
    }
    return checkStmt(WS->getBody(), UseKind::Discarded);
  }

  if (const auto *DS = dyn_cast<DoStmt>(S)) {
    if (checkStmt(DS->getBody(), UseKind::Discarded))
      return true;
    return checkExpr(DS->getCond(), UseKind::Used);
  }

  if (const auto *FS = dyn_cast<ForStmt>(S)) {
    // Init is evaluated (value discarded); the condition value is consumed; the
    // increment expression's value is discarded.
    if (const Stmt *Init = FS->getInit())
      if (checkStmt(Init, UseKind::Discarded))
        return true;
    bool BodyDead = false;
    if (const Expr *Cond = FS->getCond()) {
      if (checkExpr(Cond, UseKind::Used))
        return true;
      if (std::optional<bool> CTrue = getFoldedCondition(Cond, Ctx))
        BodyDead = !*CTrue;
    }
    // With a constant-false condition neither the increment nor the body ever
    // executes.
    if (BodyDead)
      return false;
    if (const Expr *Inc = FS->getInc())
      if (checkExpr(Inc, UseKind::Discarded))
        return true;
    return checkStmt(FS->getBody(), UseKind::Discarded);
  }

  if (const auto *SS = dyn_cast<SwitchStmt>(S)) {
    if (const Stmt *Init = SS->getInit())
      if (checkStmt(Init, UseKind::Discarded))
        return true;
    if (const Expr *Cond = SS->getCond())
      if (checkExpr(Cond, UseKind::Used))
        return true;
    return checkStmt(SS->getBody(), UseKind::Discarded);
  }

  if (const auto *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue() ? checkExpr(RS->getRetValue(), UseKind::Used)
                             : false;

  if (const auto *LS = dyn_cast<LabelStmt>(S))
    return checkStmt(LS->getSubStmt(), Use);

  if (const auto *AS = dyn_cast<AttributedStmt>(S))
    return checkStmt(AS->getSubStmt(), Use);

  if (const auto *CS = dyn_cast<CaseStmt>(S))
    return checkStmt(CS->getSubStmt(), Use);
  if (const auto *DfS = dyn_cast<DefaultStmt>(S))
    return checkStmt(DfS->getSubStmt(), Use);

  // An OpenMP/OpenACC directive subtree is exclusively the domain of the
  // MCS251 construct restriction gate (P08 revision, plan B): every bit
  // capability in its clauses or associated statement is rejected there with
  // a dedicated diagnostic. These §7.5 evaluation-aware scanners therefore
  // skip the whole subtree (clauses and captured body alike), even when the
  // enclosing full-expression walk runs outside the construct, so that the
  // old clause-evaluation classification can never become a second, conflicting
  // judge for directive input.
  if (isa<OMPExecutableDirective>(S) || isa<OpenACCConstructStmt>(S))
    return false;

  // A captured region (OpenMP/OpenACC structured block, ...) outlines its body
  // into a synthetic function. CapturedStmt::children() only exposes the
  // capture initializers -- not the body -- so both must be visited
  // explicitly. A capture initializer copies the captured value (an evaluated
  // use); the body runs as an ordinary statement sequence whose value never
  // flows out of the directive.
  if (const auto *CapS = dyn_cast<CapturedStmt>(S)) {
    for (const Stmt *Child : CapS->children())
      if (const Expr *Init = dyn_cast_or_null<Expr>(Child))
        if (checkExpr(Init, UseKind::Used))
          return true;
    return checkStmt(CapS->getCapturedStmt(), UseKind::Discarded);
  }

  // Fallback for any other statement kind (goto/indirect-goto, asm, and the
  // many C extensions: _Defer, SEH __try/__finally, ObjC @try/@autoreleasepool,
  // OpenMP/OpenACC, ...). Its evaluable parts are:
  //   * expression operands (used), and
  //   * any nested statements, which are evaluated with their value discarded
  //     (a non-expression statement has no value that flows out of this node).
  // Recurse into *statements* as well as expressions: a statement body such as
  // `_Defer { X|=1; }` must not be skipped just because its child is a compound
  // statement rather than an expression.
  for (const Stmt *Child : S->children()) {
    if (const Expr *Sub = dyn_cast_or_null<Expr>(Child)) {
      if (checkExpr(Sub, UseKind::Used))
        return true;
    } else if (Child) {
      if (checkStmt(Child, UseKind::Discarded))
        return true;
    }
  }
  return false;
}

/// Usage check of every clause operand of an OpenMP/OpenACC directive: the
/// operand's value is consumed by the directive (a condition, a count, a
/// chunk size, ...) or by the operand's own address computation (a subscript
/// index such as `depend(in: p[X ^= 1])`). Variable-list references are bare
/// lvalue references, so treating them the same way cannot mis-use a toggle.
/// This keeps the usage rules uniform across every clause-semantic group; the
/// finer read/address-only classification of the R9 round was superseded by
/// the construct boundary gate (P08 revision, plan B).
bool ControlledBitChecker::checkDirectiveClauseOperands(const Stmt *S) {
  ArrayRef<OMPClause *> OMP;
  ArrayRef<const OpenACCClause *> ACC;
  if (const auto *Dir = dyn_cast<OMPExecutableDirective>(S))
    OMP = Dir->clauses();
  else if (const auto *Dir = dyn_cast<OpenACCConstructStmt>(S))
    ACC = Dir->clauses();
  auto Check = [&](const Stmt *Node) {
    if (const Expr *Operand = dyn_cast_or_null<Expr>(Node))
      return checkExpr(Operand, UseKind::Used);
    return false;
  };
  for (const OMPClause *C : OMP) {
    // A clause with a pre-init stores the captured computations of its
    // operands (for example the `.capture_expr.' helper that hoists a runtime
    // schedule chunk size) in a statement that is not among children(). The
    // helper initializers are evaluated and consumed by the directive.
    if (const OMPClauseWithPreInit *PInit = OMPClauseWithPreInit::get(C))
      if (const Stmt *PreInit = PInit->getPreInitStmt())
        if (checkStmt(PreInit, UseKind::Discarded))
          return true;
    // The post-update of lastprivate/reduction clauses is an evaluated
    // copy-back after the region.
    if (const OMPClauseWithPostUpdate *PUpd = OMPClauseWithPostUpdate::get(C))
      if (const Expr *PostUpdate = PUpd->getPostUpdateExpr())
        if (checkExpr(PostUpdate, UseKind::Used))
          return true;
    // allocate: the allocator and alignment expressions are evaluated to
    // select the allocator, but they are stored outside children()
    // (OMPAllocateClause::children() is only the variable list), so they are
    // visited explicitly here.
    if (const auto *AC = dyn_cast<OMPAllocateClause>(C)) {
      if (const Expr *Allocator = AC->getAllocator())
        if (checkExpr(Allocator, UseKind::Used))
          return true;
      if (const Expr *Alignment = AC->getAlignment())
        if (checkExpr(Alignment, UseKind::Used))
          return true;
    }
    // Independently stored evaluated fields that children() does not include
    // (see getOMPIteratorModifier): the iterator modifier of depend/map/to/
    // from is a real runtime evaluation (its ranges expand the locator list),
    // and the linear step / calc-step are evaluated and consumed before the
    // loop. Both live in trailing slots after the varlist, so they are visited
    // explicitly here. The affinity modifier is (uniquely) inside children()
    // and is already visited by the loop below.
    if (const Expr *ItMod = getOMPIteratorModifier(C))
      if (checkExpr(ItMod, UseKind::Used))
        return true;
    if (const auto *LC = dyn_cast<OMPLinearClause>(C)) {
      if (const Expr *Step = LC->getStep())
        if (checkExpr(Step, UseKind::Used))
          return true;
      if (const Expr *CalcStep = LC->getCalcStep())
        if (checkExpr(CalcStep, UseKind::Used))
          return true;
    }
    for (const Stmt *Node : C->children())
      if (Check(Node))
        return true;
  }
  for (const OpenACCClause *C : ACC)
    for (const Stmt *Node : C->children())
      if (Check(Node))
        return true;
  return false;
}

//===----------------------------------------------------------------------===//
// Same-address read scan
//===----------------------------------------------------------------------===//

const Expr *ControlledBitChecker::findRead(const Expr *E, const Expr *LHS) {
  if (!E)
    return nullptr;

  // A controlled fixed bit reference to the same address used as a value is a
  // read.
  if (isMCS251ControlledBitLValue(E, Ctx) && isSameMCS251FixedBit(E, LHS, Ctx))
    return E;

  if (isa<UnaryExprOrTypeTraitExpr>(E))
    return nullptr;

  // A builtin whose arguments are unevaluated (__builtin_constant_p,
  // __builtin_classify_type, ...) does not read the bit at runtime either:
  // `X = __builtin_constant_p(X)` is not a self-read. This mirrors the skip in
  // checkExpr and the constant-initializer guard.
  if (isUnevaluatedBuiltinCall(E, Ctx))
    return nullptr;

  if (const auto *GSE = dyn_cast<GenericSelectionExpr>(E)) {
    if (GSE->isResultDependent())
      return nullptr;
    return findRead(GSE->getResultExpr(), LHS);
  }
  if (const auto *CE = dyn_cast<ChooseExpr>(E))
    return findRead(CE->getChosenSubExpr(), LHS);

  if (const auto *SE = dyn_cast<StmtExpr>(E))
    return findReadInStmt(SE->getSubStmt(), LHS);

  // The LHS of an assignment is a *write* target, not a read; only scan the
  // RHS plus the LHS's address computation (a subscript/member base such as
  // `a[X]` still reads X to compute the address).
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp()) {
      if (const Expr *Found = findRead(BO->getRHS(), LHS))
        return Found;
      return findReadAddressComputation(BO->getLHS(), LHS);
    }
    // Short-circuit: the LHS is always evaluated; a constant LHS that decides
    // the result makes the RHS dead.
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr) {
      if (const Expr *Found = findRead(BO->getLHS(), LHS))
        return Found;
      if (std::optional<bool> LTrue = getFoldedCondition(BO->getLHS(), Ctx)) {
        bool ShortCircuits = (BO->getOpcode() == BO_LAnd) ? !*LTrue : *LTrue;
        if (ShortCircuits)
          return nullptr;
      }
      return findRead(BO->getRHS(), LHS);
    }
  }
  // ++X / --X both read and write: the value is read, and the operand's address
  // computation is evaluated.
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->isIncrementDecrementOp())
      return findReadAddressComputation(UO->getSubExpr(), LHS);
  }

  // Ordinary `c ? a : b` with a constant condition: only the selected branch
  // is evaluated.
  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    if (const Expr *Found = findRead(CO->getCond(), LHS))
      return Found;
    if (std::optional<bool> CTrue = getFoldedCondition(CO->getCond(), Ctx))
      return findRead(*CTrue ? CO->getTrueExpr() : CO->getFalseExpr(), LHS);
    if (const Expr *Found = findRead(CO->getTrueExpr(), LHS))
      return Found;
    return findRead(CO->getFalseExpr(), LHS);
  }

  // GNU `c ?: x`: the common expression is always evaluated; a constant
  // non-zero common makes the false expression dead.
  if (const auto *BCO = dyn_cast<BinaryConditionalOperator>(E)) {
    if (const Expr *Found = findRead(BCO->getCommon(), LHS))
      return Found;
    if (std::optional<bool> CTrue = getFoldedCondition(BCO->getCommon(), Ctx);
        CTrue && *CTrue)
      return nullptr;
    return findRead(BCO->getFalseExpr(), LHS);
  }

  for (const Stmt *Child : E->children())
    if (const Expr *Sub = dyn_cast_or_null<Expr>(Child))
      if (const Expr *Found = findRead(Sub, LHS))
        return Found;
  return nullptr;
}

/// Scan the *address computation* of a write target \p E: read nothing from the
/// target lvalue itself, but evaluate (and thus read) the sub-expressions that
/// compute its address, e.g. the index in `a[X]`, the base in `p->m`, or the
/// whole pointer expression of `*(p + X)`.
const Expr *ControlledBitChecker::findReadAddressComputation(const Expr *E,
                                                             const Expr *LHS) {
  if (!E)
    return nullptr;
  E = E->IgnoreParenImpCasts();
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    if (const Expr *Found = findRead(ASE->getIdx(), LHS))
      return Found;
    return findRead(ASE->getBase(), LHS);
  }
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return findRead(ME->getBase(), LHS);
  // `*addr`: the entire operand computes the address and is evaluated, so
  // `*(p + X)`, `*h(X)`, and `*(X ? p : p + 1)` all read X.
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Deref)
      return findRead(UO->getSubExpr(), LHS);
  }
  // An OpenMP/OpenACC array section `p[lo:len]` used as a locator (depend,
  // create, copy, ...): the base pointer, the lower bound, and the length are
  // all evaluated to compute the section's address, but the section itself is
  // not read.
  if (const auto *ASE = dyn_cast<ArraySectionExpr>(E)) {
    if (const Expr *Found = findRead(ASE->getLowerBound(), LHS))
      return Found;
    if (const Expr *Found = findRead(ASE->getLength(), LHS))
      return Found;
    return findRead(ASE->getBase(), LHS);
  }
  // An OpenMP array-shaping expression `([n][m])p` in a locator: each
  // dimension is an independently evaluated expression (stored before the
  // base in the trailing objects, and both are in children()), and together
  // with the base they compute the address. The section itself is not read.
  if (const auto *Shaping = dyn_cast<OMPArrayShapingExpr>(E)) {
    for (const Expr *Dim : Shaping->getDimensions())
      if (const Expr *Found = findRead(Dim, LHS))
        return Found;
    return findRead(Shaping->getBase(), LHS);
  }
  // An OpenCL extended-vector swizzle writes through its base lvalue; only the
  // base computes the address.
  if (const auto *EVE = dyn_cast<ExtVectorElementExpr>(E))
    return findRead(EVE->getBase(), LHS);
  // A plain lvalue (fixed bit, variable) has no address computation that
  // evaluates any sub-expression we care about.
  return nullptr;
}

const Expr *ControlledBitChecker::findReadInStmt(const Stmt *S,
                                                 const Expr *LHS) {
  if (!S)
    return nullptr;
  if (const auto *E = dyn_cast<Expr>(S))
    return findRead(E, LHS);
  if (const auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls())
      if (const auto *VD = dyn_cast<VarDecl>(D))
        if (const Expr *Init = VD->getInit())
          if (const Expr *Found = findRead(Init, LHS))
            return Found;
    return nullptr;
  }
  if (const auto *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue() ? findRead(RS->getRetValue(), LHS) : nullptr;

  // case/default labels are integer constant expressions: they are never
  // evaluated at runtime, so only the sub-statement is scanned. This mirrors
  // checkStmt and the constant-initializer guard.
  if (const auto *CS = dyn_cast<CaseStmt>(S))
    return findReadInStmt(CS->getSubStmt(), LHS);
  if (const auto *DfS = dyn_cast<DefaultStmt>(S))
    return findReadInStmt(DfS->getSubStmt(), LHS);

  // An OpenMP/OpenACC directive subtree is skipped entirely (see checkStmt):
  // the construct restriction gate owns its clauses and captured body.
  if (isa<OMPExecutableDirective>(S) || isa<OpenACCConstructStmt>(S))
    return nullptr;

  // A captured region's body is not among CapturedStmt::children() (see
  // checkStmt). The capture initializers that ARE the children() are paired
  // with the capture kind: a by-copy initializer loads the captured value and
  // reads any same-bit operand, while a by-reference initializer only binds
  // the storage (the lastprivate copy-back target, a shared variable) and
  // merely computes its address.
  if (const auto *CapS = dyn_cast<CapturedStmt>(S)) {
    const CapturedStmt::Capture *Cap = CapS->capture_begin();
    for (const Stmt *Child : CapS->children()) {
      const Expr *Init = dyn_cast_or_null<Expr>(Child);
      if (!Init)
        continue;
      bool ByReference = Cap != CapS->capture_end() &&
                         Cap->capturesVariable(); // VCK_ByRef
      const Expr *Found =
          ByReference ? findReadAddressComputation(Init, LHS)
                      : findRead(Init, LHS);
      if (Found)
        return Found;
      if (Cap != CapS->capture_end())
        ++Cap;
    }
    return findReadInStmt(CapS->getCapturedStmt(), LHS);
  }

  // Statement-level dead-branch folding, mirroring checkStmt: the dead branch
  // of an `if (0)`/`while (0)`/`for (; 0;)` never reads the bit.
  if (const auto *IS = dyn_cast<IfStmt>(S)) {
    if (const Stmt *Init = IS->getInit())
      if (const Expr *Found = findReadInStmt(Init, LHS))
        return Found;
    std::optional<bool> CondTrue;
    if (const Expr *Cond = IS->getCond()) {
      if (const Expr *Found = findRead(Cond, LHS))
        return Found;
      CondTrue = getFoldedCondition(Cond, Ctx);
    }
    if (CondTrue) {
      const Stmt *Live = *CondTrue ? IS->getThen() : IS->getElse();
      return Live ? findReadInStmt(Live, LHS) : nullptr;
    }
    if (const Expr *Found = findReadInStmt(IS->getThen(), LHS))
      return Found;
    if (const Stmt *Else = IS->getElse())
      return findReadInStmt(Else, LHS);
    return nullptr;
  }
  if (const auto *WS = dyn_cast<WhileStmt>(S)) {
    if (const Expr *Cond = WS->getCond()) {
      if (const Expr *Found = findRead(Cond, LHS))
        return Found;
      if (std::optional<bool> CTrue = getFoldedCondition(Cond, Ctx);
          CTrue && !*CTrue)
        return nullptr;
    }
    return findReadInStmt(WS->getBody(), LHS);
  }
  if (const auto *FS = dyn_cast<ForStmt>(S)) {
    if (const Stmt *Init = FS->getInit())
      if (const Expr *Found = findReadInStmt(Init, LHS))
        return Found;
    bool BodyDead = false;
    if (const Expr *Cond = FS->getCond()) {
      if (const Expr *Found = findRead(Cond, LHS))
        return Found;
      if (std::optional<bool> CTrue = getFoldedCondition(Cond, Ctx))
        BodyDead = !*CTrue;
    }
    if (BodyDead)
      return nullptr;
    if (const Expr *Inc = FS->getInc())
      if (const Expr *Found = findRead(Inc, LHS))
        return Found;
    return findReadInStmt(FS->getBody(), LHS);
  }

  // An asm statement reads its input operands; an output operand is a write
  // target whose address computation is evaluated, but the target lvalue itself
  // is not read. A read-write output (`+r`) is different: the operand's value
  // is loaded into the constraint before the asm runs, so it is a read as well.
  if (const auto *AS = dyn_cast<GCCAsmStmt>(S)) {
    for (unsigned I = 0, E = AS->getNumInputs(); I != E; ++I)
      if (const Expr *Found = findRead(AS->getInputExpr(I), LHS))
        return Found;
    for (unsigned I = 0, E = AS->getNumOutputs(); I != E; ++I) {
      StringRef Constraint = AS->getOutputConstraint(I);
      if (!Constraint.empty() && Constraint.front() == '+') {
        if (const Expr *Found = findRead(AS->getOutputExpr(I), LHS))
          return Found;
      } else if (const Expr *Found =
                     findReadAddressComputation(AS->getOutputExpr(I), LHS)) {
        return Found;
      }
    }
    return nullptr;
  }
  for (const Stmt *Child : S->children())
    if (const Expr *Found = findReadInStmt(Child, LHS))
      return Found;
  return nullptr;
}

bool SemaMCS251::checkControlledBitExpr(Expr *E, bool ResultUsed) {
  ControlledBitChecker Checker(*this);
  return Checker.checkExpr(E, ResultUsed ? UseKind::Used : UseKind::Discarded);
}

bool SemaMCS251::CheckMCS251ControlledBitRMW(Expr *E, bool DiscardedValue) {
  // P08 revision (plan B): inside an OpenMP/OpenACC construct restriction
  // context every bit capability has already been rejected by the construct
  // gate at its own finite entry points, so the §7.5 evaluation-aware
  // classification below must not run here and must not become a second,
  // conflicting judge for the same input.
  if (inDirectiveRestriction())
    return false;

  // A statement inside a GNU statement expression is checked by the parser via
  // ActOnFinishFullExpr before the enclosing full expression -- which decides
  // whether the statement expression's value is used -- is known. Skip that
  // inner call; the enclosing full expression re-walks the whole StmtExpr with
  // the correct result usage. Any enclosing statement-expression scope counts,
  // including through nested ordinary blocks. The search also crosses captured
  // regions (OpenMP/OpenACC structured blocks): the region body runs as part of
  // the directive's evaluation, so the enclosing full expression walk covers it
  // and the inner call must defer to avoid diagnosing the same statement twice.
  // A block or lambda boundary ends the search: those bodies run at invocation
  // time, not as part of the enclosing full expression.
  for (auto It = SemaRef.FunctionScopes.rbegin(),
            End = SemaRef.FunctionScopes.rend();
       It != End; ++It) {
    sema::FunctionScopeInfo *FSI = *It;
    if (isa<sema::BlockScopeInfo>(FSI) || isa<sema::LambdaScopeInfo>(FSI))
      break;
    for (const auto &Scope : FSI->CompoundScopes)
      if (Scope.IsStmtExpr)
        return false;
  }

  return checkControlledBitExpr(E, /*ResultUsed=*/!DiscardedValue);
}

bool SemaMCS251::CheckMCS251ControlledBitDirectiveClauses(Stmt *Directive) {
  // P08 revision (plan B): same as CheckMCS251ControlledBitRMW -- the
  // construct gate owns every bit capability inside a directive, so the
  // clause-evaluation classification is suppressed there (kept for
  // non-construct input and for review history).
  if (inDirectiveRestriction())
    return false;

  // Clause operands are not full expressions for most clause kinds (only some
  // clause parse paths route through ActOnFinishFullExpr), so a directive
  // outside any statement expression is checked here once at completion. A
  // directive inside a statement expression defers, exactly like
  // CheckMCS251ControlledBitRMW: the enclosing full-expression walk reaches
  // the directive and scans its clauses with the correct result usage, so the
  // inner call must not diagnose twice.
  for (auto It = SemaRef.FunctionScopes.rbegin(),
            End = SemaRef.FunctionScopes.rend();
       It != End; ++It) {
    sema::FunctionScopeInfo *FSI = *It;
    if (isa<sema::BlockScopeInfo>(FSI) || isa<sema::LambdaScopeInfo>(FSI))
      break;
    for (const auto &Scope : FSI->CompoundScopes)
      if (Scope.IsStmtExpr)
        return false;
  }

  ControlledBitChecker Checker(*this);
  return Checker.checkDirectiveClauseOperands(Directive);
}

//===----------------------------------------------------------------------===//
// OpenMP/OpenACC construct restriction context (P08 revision, plan B)
//===----------------------------------------------------------------------===//

/// The restriction context only exists on the MCS251 target: everywhere else
/// the `bit`/`sbit` capability does not exist and entering would be dead
/// weight.
static bool isMCS251Target(const ASTContext &Ctx) {
  return Ctx.getTargetInfo().getTriple().getArch() == llvm::Triple::mcs251;
}

void SemaMCS251::enterDirectiveRestriction(SourceLocation DirectiveLoc,
                                           bool IsOpenACC, bool Persistent) {
  // Only the MCS251 target has the bit capability at all; elsewhere the
  // context (and its checks) stays off, keeping the hot entry points of
  // other targets untouched.
  if (!isMCS251Target(getASTContext()))
    return;
  RestrictionFrames.push_back(
      {DirectiveLoc, IsOpenACC, Persistent, /*Diagnosed=*/false});
}

/// Remove the innermost frame of one class (parser per-directive vs
/// persistent region) from the interleaved stack. Each class is left
/// strictly by its own exit, so nesting stays balanced on every recovery
/// path. The per-construct diagnostic latch lives in the frame, so leaving a
/// nested construct restores the outer construct's own latch state exactly.
void SemaMCS251::leaveFrame(bool Persistent) {
  for (unsigned I = RestrictionFrames.size(); I > 0; --I) {
    if (RestrictionFrames[I - 1].Persistent == Persistent) {
      RestrictionFrames.erase(RestrictionFrames.begin() + (I - 1));
      break;
    }
  }
}

void SemaMCS251::exitDirectiveRestriction() { leaveFrame(/*Persistent=*/false); }

void SemaMCS251::exitPersistentDirectiveRestriction() {
  leaveFrame(/*Persistent=*/true);
}

void SemaMCS251::drainDirectiveRestrictions() { RestrictionFrames.clear(); }

bool SemaMCS251::diagnoseBitCapabilityInDirective(SourceLocation Loc,
                                                  SourceRange Range) {
  if (RestrictionFrames.empty() || RestrictionFrames.back().Diagnosed)
    return false;
  RestrictionFrames.back().Diagnosed = true;
  const RestrictionFrame &Inner = RestrictionFrames.back();
  Diag(Loc, Inner.IsOpenACC ? diag::err_mcs251_bit_acc_construct
                            : diag::err_mcs251_bit_omp_construct)
      << Range;
  Diag(Inner.DirectiveLoc, diag::note_mcs251_bit_construct_here)
      << (Inner.IsOpenACC ? 1 : 0);
  return true;
}

/// Does the function(-like) signature \p FnTy carry the MCS251 bit capability:
/// a bit return type, a bit parameter, or bit nested inside a return/parameter
/// type reachable through further (block-)pointer-to-signature and array
/// chains?
static bool typeCarriesMCS251Bit(QualType T);

static bool signatureCarriesMCS251Bit(QualType FnTy) {
  const auto *FT = FnTy.getCanonicalType()->getAs<FunctionType>();
  if (!FT)
    return typeCarriesMCS251Bit(FnTy);
  if (typeCarriesMCS251Bit(FT->getReturnType()))
    return true;
  if (const auto *FPT = dyn_cast<FunctionProtoType>(FT))
    for (QualType PT : FPT->param_types())
      if (typeCarriesMCS251Bit(PT))
        return true;
  return false;
}

/// Does \p T carry the MCS251 bit capability at type granularity: the bit
/// scalar itself, or bit nested inside a function-pointer/block-pointer
/// signature or an array reachable from T (final-rework F1: a signature
/// carrying bit is the same capability as a bit object -- `typedef __bit
/// (*F)(void)` launders neither a reference nor a call). Plain pointers to
/// bit and arrays of bit are rejected by the ordinary SemaType rules; the
/// walk stays conservative about them anyway. Record interiors are not
/// traversed: a struct merely containing a bit-signature field is not itself
/// the capability (a field reference or a newly written field declaration
/// is checked at its own entry).
///
/// Termination (final rework F6): there is no depth cutoff. The walk only
/// follows pointer chains, arrays, signatures, and the single atomic wrapper
/// layer, and C type graphs are acyclic without records -- recursion into a
/// record is the only way to build a cycle, and records are not traversed.
/// The atomic layer cannot form a chain of its own: `_Atomic(_Atomic(T))` is
/// ill-formed in C (C11 6.7.2.4p1: the type named by an _Atomic specifier
/// shall not be atomic-qualified), so each type carries at most one outer
/// atomic wrapper. A deep `typedef F *******P` chain therefore cannot
/// out-walk the gate; the earlier cutoff that returned false at depth 8 was
/// an escape hatch, not a guard.
static bool typeCarriesMCS251Bit(QualType T) {
  if (T.isNull())
    return false;
  QualType Can = T.getCanonicalType();
  if (Can->isMCS251BitType())
    return true;
  if (Can->isFunctionType()) // e.g. `typedef __bit F(void);`
    return signatureCarriesMCS251Bit(Can);
  if (const auto *PT = dyn_cast<PointerType>(Can)) {
    QualType Pointee = PT->getPointeeType();
    if (Pointee->isFunctionType())
      return signatureCarriesMCS251Bit(Pointee);
    return typeCarriesMCS251Bit(Pointee);
  }
  if (const auto *BPT = dyn_cast<BlockPointerType>(Can))
    return signatureCarriesMCS251Bit(BPT->getPointeeType());
  // Final rework F9: `_Atomic(F)` is a legal spelling of the same capability
  // when F carries bit -- the atomic wrapper hides the pointer/signature from
  // the walk above, so recurse into the value type. The direct `_Atomic(__bit)`
  // spelling stays governed by the ordinary SemaType rule and is unchanged.
  if (const auto *AtomicT = dyn_cast<AtomicType>(Can))
    return typeCarriesMCS251Bit(AtomicT->getValueType());
  if (const auto *AT = dyn_cast<ArrayType>(Can))
    return typeCarriesMCS251Bit(AT->getElementType());
  return false;
}

/// Does \p ND (a declare-target/routine/simd/variant argument, a member
/// field, or a function definition) carry the bit capability at declaration
/// granularity: a bit-typed object (through typedef/cv and through
/// function-pointer/block-pointer signatures and arrays), a fixed-address
/// sbit, or a function whose signature involves the bit type?
static bool carriesMCS251BitCapability(const NamedDecl *ND) {
  if (const auto *VD = dyn_cast<VarDecl>(ND)) // covers ParmVarDecl
    return VD->hasAttr<MCS251BitAddressAttr>() ||
           typeCarriesMCS251Bit(VD->getType());
  if (const auto *FD = dyn_cast<FieldDecl>(ND))
    return typeCarriesMCS251Bit(FD->getType());
  if (const auto *TD = dyn_cast<TypedefNameDecl>(ND))
    return typeCarriesMCS251Bit(TD->getUnderlyingType());
  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    return signatureCarriesMCS251Bit(FD->getType());
  return false;
}

void SemaMCS251::CheckDeclRefInDirectiveRestriction(const ValueDecl *D,
                                                    SourceLocation Loc) {
  if (!carriesMCS251BitCapability(D))
    return;
  diagnoseBitCapabilityInDirective(Loc, D->getSourceRange());
}

void SemaMCS251::CheckNamedDeclArgumentInDirectiveRestriction(
    const NamedDecl *ND, SourceLocation Loc) {
  // A directive argument names the declaration through directive-specific
  // lookup rather than an ordinary reference; the same semantic-identity
  // rule applies.
  if (carriesMCS251BitCapability(ND))
    diagnoseBitCapabilityInDirective(Loc, Loc);
}

void SemaMCS251::CheckDeclaratorInDirectiveRestriction(const Decl *D) {
  if (carriesMCS251BitCapability(cast<NamedDecl>(D)))
    diagnoseBitCapabilityInDirective(D->getLocation(), D->getSourceRange());
}

void SemaMCS251::CheckSbitDeclInDirectiveRestriction(SourceLocation Loc,
                                                     SourceRange Range) {
  // The sbit parse path has not created the declaration yet; the identity is
  // established by the parse itself.
  diagnoseBitCapabilityInDirective(Loc, Range);
}

void SemaMCS251::CheckBitBuiltinInDirectiveRestriction(SourceLocation Loc,
                                                       SourceRange Range) {
  diagnoseBitCapabilityInDirective(Loc, Range);
}

void SemaMCS251::CheckBlockSignatureInDirectiveRestriction(
    QualType FnTy, SourceLocation Loc, SourceRange Range) {
  // Final rework F2: a block literal's signature is construct input. Block
  // signatures are established in ActOnBlockArguments and do not pass through
  // HandleDeclarator, so this is their dedicated entry. A deduced (absent)
  // return type cannot smuggle bit in: any bit-valued return expression has
  // to name bit somewhere and is caught by the reference/cast entries.
  if (signatureCarriesMCS251Bit(FnTy))
    diagnoseBitCapabilityInDirective(Loc, Range);
}

void SemaMCS251::CheckVAArgTypeInDirectiveRestriction(QualType T,
                                                      SourceLocation Loc,
                                                      SourceRange Range) {
  // Final rework F3: a bit (or bit-carrying) __builtin_va_arg type argument
  // establishes a bit value inside the construct. Outside constructs the
  // va_arg-of-bit M2 gap stays as registered (accepted, compiled as an i8
  // slot); the construct prohibition does not inherit that gap.
  if (typeCarriesMCS251Bit(T))
    diagnoseBitCapabilityInDirective(Loc, Range);
}

void SemaMCS251::CheckTypeInputInDirectiveRestriction(QualType T,
                                                      SourceLocation Loc,
                                                      SourceRange Range) {
  // Final rework F5/F7: one shared entry for every pure type input of the
  // construct -- the type of an explicit cast or compound literal (a
  // bit-signature pointer type is the same capability as bit itself), a
  // type-trait argument, a _Generic controlling/association type, and a
  // sizeof/_Alignof type operand, or a newly written C record member (F10).
  // Evaluation-blind per the P08 boundary.
  if (typeCarriesMCS251Bit(T))
    diagnoseBitCapabilityInDirective(Loc, Range);
}

Sema::EnterMCS251DirectiveRestriction::EnterMCS251DirectiveRestriction(
    Sema &S, SourceLocation DirectiveLoc, bool IsOpenACC)
    : S(S), Entered(true) {
  // enterDirectiveRestriction is a no-op on non-MCS251 targets, and the
  // paired exitDirectiveRestriction is a no-op on an empty stack, so the
  // pair stays balanced on every target.
  S.MCS251Ptr->enterDirectiveRestriction(DirectiveLoc, IsOpenACC);
}

// A function definition belongs to a declarative construct when the function
// carries the construct's mark: the definition region then inherits the
// restriction even when it is textually parsed far away from the pragma. The
// mark is searched on the whole redeclaration chain because either
// declaration may carry it. The complete "mark a prototype, define remotely"
// family of this tree (audited in the final rework, F4):
//   * OpenMP declare target           OMPDeclareTargetDeclAttr   (inherited)
//   * OpenMP declare simd             OMPDeclareSimdDeclAttr      (per-decl;
//     found through the chain walk)
//   * OpenMP declare variant          OMPDeclareVariantAttr       (inherited;
//     the delimited begin/end declare variant region is *additionally* a
//     persistent restriction region, so declarations inside it are covered
//     during the region parse itself)
//   * OpenACC routine                 OpenACCRoutineDeclAttr / ...AnnotAttr
// Rejected as members of the family, after checking their semantics: declare
// mapper (its associated declaration is created inside the directive parse;
// there is no remote definition), threadprivate/allocate (variables: no
// function-definition ownership; the directive-time reference check covers
// the listed names), declare reduction (identity created inside the parse),
// requires/assumes (no associated declaration).
static const Attr *getDeclarativeConstructMark(const Decl *Fn) {
  const auto *FD = dyn_cast<FunctionDecl>(Fn);
  if (!FD)
    return nullptr;
  for (const Decl *D = FD->getMostRecentDecl(); D; D = D->getPreviousDecl()) {
    if (const auto *Mark = D->getAttr<OMPDeclareTargetDeclAttr>())
      return Mark;
    if (const auto *Simd = D->getAttr<OMPDeclareSimdDeclAttr>())
      return Simd;
    if (const auto *Variant = D->getAttr<OMPDeclareVariantAttr>())
      return Variant;
    if (const auto *R = D->getAttr<OpenACCRoutineDeclAttr>())
      return R;
    if (const auto *RA = D->getAttr<OpenACCRoutineAnnotAttr>())
      return RA;
  }
  return nullptr;
}

Sema::EnterMCS251DirectiveRestriction::EnterMCS251DirectiveRestriction(
    Sema &S, const Decl *Fn)
    : S(S), Entered(false) {
  if (!isMCS251Target(S.getASTContext()))
    return;
  const Attr *Mark = getDeclarativeConstructMark(Fn);
  if (!Mark)
    return;
  Entered = true;
  bool IsOpenACC = isa<OpenACCRoutineDeclAttr>(Mark) ||
                   isa<OpenACCRoutineAnnotAttr>(Mark);
  S.MCS251Ptr->enterDirectiveRestriction(Mark->getLocation(), IsOpenACC);
  // The definition's own signature was sema'd (HandleDeclarator) before this
  // constructor ran, so check it here explicitly: a bit return type or bit
  // parameter of the owned definition is itself construct input.
  S.MCS251Ptr->CheckDeclaratorInDirectiveRestriction(Fn);
}

Sema::EnterMCS251DirectiveRestriction::~EnterMCS251DirectiveRestriction() {
  if (Entered)
    S.MCS251Ptr->exitDirectiveRestriction();
}

} // namespace clang
