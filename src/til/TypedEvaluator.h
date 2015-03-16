//===- TypedEvaluator.h ----------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_TYPEDEVALUATOR_H
#define OHMU_TIL_TYPEDEVALUATOR_H

#include "TIL.h"
#include "TILTraverse.h"
#include "AttributeGrammar.h"
#include "CopyReducer.h"

#include <queue>


namespace ohmu {
namespace til  {

class ScopeCPS;


/// TypedCopyAttr holds the attributes for TypedEvaluator.
/// TypedEvalutor will rewrite each term  t  to    u <=: [x*=s*]v   where:
///
///   u       is the residual (i.e. partially evaluated) version of t
///   <=:     is a relation (typing, subtyping, equivalence)
///   v       is a value (e.g. record/function), which holds the type of u.
///   [x*=s*] is the substitution of s* for x* in v
///
/// Rewriting will copy terms from a source scope to a destination scope by
/// substituting residuals in the destination scope for variables in the source
/// scope.  Substitution in  'u'  is done eagerly.  Substitution for types,
/// however, is lazy.  By substituting lazily, we can avoid a lot of
/// unnecessary rewriting.
///
/// Note that we omit the type expr for base types, e.g. int, float, string;
/// those are stored in the instruction itself.
///
class TypedCopyAttr : public CopyAttr {
public:
  enum Relation {
    BT_Type,           ///<  Term has type, or is a subtype of, TypeExpr
    BT_ExactType,      ///<  Term has exact type TypeExpr
    BT_Equivalent      ///<  Term is equivalent to TypeExpr
  };

  static Relation minRelation(Relation r1, Relation r2) {
    return (r1 < r2) ? r1 : r2;
  }

public:
  TypedCopyAttr()
      : TypeExpr(nullptr), Rel(BT_Type)
  { }
  explicit TypedCopyAttr(SExpr *E)
      : CopyAttr(E), TypeExpr(nullptr), Rel(BT_Type)
  { }

  TypedCopyAttr(const TypedCopyAttr &A) = default;
  TypedCopyAttr& operator=(const TypedCopyAttr &A) = default;

  TypedCopyAttr(TypedCopyAttr &&A) : CopyAttr(A),
      TypeExpr(A.TypeExpr), Subst(std::move(A.Subst)), Rel(A.Rel) {
    A.TypeExpr = nullptr;
    A.Rel      = BT_Type;
  }

  void operator=(TypedCopyAttr &&A) {
    Exp        = A.Exp;
    TypeExpr   = A.TypeExpr;
    Rel        = A.Rel;
    Subst      = std::move(A.Subst);
    A.TypeExpr = nullptr;
    A.Rel      = BT_Type;
  }

  // Push a variable substitution onto the stack.
  void pushSubst(const TypedCopyAttr &At) {
    Subst.push_back(At);
  }
  void pushSubst(TypedCopyAttr &&At) {
    Subst.push_back( std::move(At) );
  }

  // Steal the substitution list from another attribute.
  void stealSubstitution(TypedCopyAttr &A) {
    Subst      = std::move(A.Subst);
    A.TypeExpr = nullptr;
    A.Rel      = BT_Type;
  }

  // Steal the type information from another attribute.
  // Implements transitivity and subsumption, e.g.  (t : (u <: T)) --> t : T
  void moveType(TypedCopyAttr& A) {
    TypeExpr   = A.TypeExpr;
    Subst      = std::move(A.Subst);
    Rel        = minRelation(Rel, A.Rel);
    A.TypeExpr = nullptr;
    A.Rel      = BT_Type;
  }

public:
  SExpr*    TypeExpr;   ///< The type of the term; should be a value.
  Substitution<TypedCopyAttr> Subst;    ///< Substitution for TypeExpr
  Relation  Rel;        ///< How the type is related to the term.
};



/// ScopeCPS extends ScopeFrame to hold the current continuation.
class ScopeCPS : public CopyScope<TypedCopyAttr, BasicBlock*> {
public:
  typedef CopyScope<TypedCopyAttr, BasicBlock*> Super;

  BasicBlock* currentContinuation() const { return Cont; }
  void setCurrentContinuation(BasicBlock* C) { Cont = C; }

  // Called by AGTraversal::traverse().
  BasicBlock* enterSubExpr(TraversalKind K) {
    if (K != TRV_Tail) {
      BasicBlock* Temp = Cont;
      Cont = nullptr;
      return Temp;
    }
    return nullptr;
  }

  // Called by AGTraversal::traverse().
  void exitSubExpr(TraversalKind K, BasicBlock* C) {
    if (K != TRV_Tail)
      Cont = C;
  }

  /// Create a copy of this scope.  (Used for lazy rewriting)
  ScopeCPS* clone() { return new ScopeCPS(*this); }

  ScopeCPS() : Cont(nullptr) { }
  ScopeCPS(Substitution<TypedCopyAttr> &&Subst)
    : Super(std::move(Subst)), Cont(nullptr)
  { }

protected:
  ScopeCPS(const ScopeCPS& S) = default;

private:
  BasicBlock* Cont;
};


/// Holds information needed for a block that's being lazily rewritten.
struct PendingBlock {
  SExpr*      Exp;
  BasicBlock* Block;
  ScopeCPS*   Scope;
  BasicBlock* Cont;

  PendingBlock(SExpr *E, BasicBlock *B, ScopeCPS *S)
      : Exp(E), Block(B), Scope(S), Cont(nullptr)
  { }
  ~PendingBlock() {
    if (Scope)
      delete Scope;
  }
};


class CFGFuture;


/// TypedEvaluator will rewrite a high-level ohmu AST to a CFG.
class TypedEvaluator
    : public CopyReducer<TypedCopyAttr, ScopeCPS>,
      public LazyCopyTraversal<TypedEvaluator, ScopeCPS>
{
private:
  typedef CopyReducer<TypedCopyAttr, ScopeCPS> Super;
  typedef LazyCopyTraversal<TypedEvaluator, ScopeCPS> SuperTv;

public:
  DiagnosticEmitter& diag() { return Builder.diag(); }

  /** reduceX(...) methods */

  void reduceScalarType(ScalarType *Orig);

  template<class T>
  void reduceLiteralT(LiteralT<T> *Orig);

  void reduceFunction(Function   *Orig);
  void reduceRecord  (Record     *Orig);
  void reduceCode    (Code       *Orig);
  void reduceField   (Field      *Orig);

  void reduceVariable(Variable   *Orig);
  void reduceProject (Project    *Orig);
  void reduceApply   (Apply      *Orig);
  void reduceCall    (Call       *Orig);
  void reduceLoad    (Load       *Orig);
  void reduceUnaryOp (UnaryOp    *Orig);
  void reduceBinaryOp(BinaryOp   *Orig);

  void reduceIdentifier(Identifier *Orig);

  /** traverseX(...) methods */

  template<class T>
  void traverse(T* E, TraversalKind K);

  void traverseFunction(Function *Orig);
  void traverseRecord  (Record   *Orig);
  void traverseCode    (Code     *Orig);
  void traverseField   (Field    *Orig);

  void traverseLet       (Let *Orig);
  void traverseIfThenElse(IfThenElse *Orig);

  void traverseFuture(Future *Orig);

private:
  friend class CFGFuture;

  void evaluateTypeExpr(TypedCopyAttr &At);
  void computeAttrType (TypedCopyAttr &At, SExpr *E);
  void promoteVariable (Variable *V);
  bool checkAndExtendTypes(Instruction *&I0, Instruction *&I1);

  void traverseNestedCode(Code* Orig);
  bool reduceNestedCall(Call* Orig, Code* C);
  void processPendingBlocks();

  enum EvaluationMode {
    TEval_Copy,      ///< Do a deep copy of a term, traversing inside values
    TEval_WeakHead   ///< Evaluate to weak-head; do not traverse inside values
  };

  EvaluationMode switchEvalMode(EvaluationMode M) {
    auto Tmp = EvalMode;  EvalMode = M;  return Tmp;
  }

  void restoreEvalMode(EvaluationMode M) { EvalMode = M; }

public:
  TypedEvaluator(MemRegionRef A)
    : Super(A), EvalMode(TEval_Copy)
  { }

protected:
  EvaluationMode                             EvalMode;
  std::vector<std::unique_ptr<PendingBlock>> PendingBlks;
  std::queue<PendingBlock*>                  PendingBlockQueue;
  DenseMap<Code*, PendingBlock*>             CodeMap;
};


template<class T>
void TypedEvaluator::reduceLiteralT(LiteralT<T> *Orig) {
  // Don't copy literals unless deep copy has been requested.
  LiteralT<T>* Re;
  if (EvalMode == TEval_WeakHead)
    Re = Orig;
  else
    Re = Builder.newLiteralT<T>(Orig->value());

  auto& Res = resultAttr();
  Res.Exp      = Re;
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Re;
}


template<class T>
void TypedEvaluator::traverse(T* E, TraversalKind K) {
  SuperTv::traverse(E, K);

  // If we were called with a continuation, then end the current block
  // with a jump to the continuation.
  if (K == TRV_Tail && scope()->currentContinuation() && Builder.currentBB()) {
    Builder.newGoto(scope()->currentContinuation(), lastAttr().Exp);
  }
}



}  // end namespace til
}  // end namespace ohmu


#endif  // SRC_TIL_TYPEDEVALUATOR_H_
