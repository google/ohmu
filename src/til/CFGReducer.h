//===- CFGReducer.h --------------------------------------------*- C++ --*-===//
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
//
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_CFGREDUCER_H
#define OHMU_TIL_CFGREDUCER_H

#include "CopyReducer.h"

#include <cstddef>
#include <memory>
#include <queue>
#include <vector>

namespace ohmu {
namespace til  {


/// Holds the type of a term, which is a value that describes the type.
/// Values include ScalarType, Function, Record, Code, Field.
struct BoundingType {
  enum Relation {
    BT_Type,           ///<  Term has type TypeExpr
    BT_ExactType,      ///<  Term has exact type TypeExpr
    BT_Equivalent      ///<  Term is equivalent to TypeExpr
  };

  static Relation minRelation(Relation r1, Relation r2) {
    return (r1 < r2) ? r1 : r2;
  }

  void clear() {
    TypeExpr = nullptr;
    Rel = BT_Type;
  }

  void set(SExpr* e) { TypeExpr = e; }
  void set(SExpr* e, Relation r) { TypeExpr = e; Rel = r; }

  SExpr* typeExpr() const { return TypeExpr; }

private:
  SExpr* TypeExpr;     ///< The type expression for the term.
  Relation Rel;        ///< How the type is related to the term.
};



/// Holds information needed for a block that's being lazily rewritten.
struct PendingBlock {
  SExpr*      expr;
  BasicBlock* block;
  BasicBlock* continuation;
  ScopeFrame* scope;

  PendingBlock(SExpr *e, BasicBlock *b, ScopeFrame* s)
    : expr(e), block(b), continuation(nullptr), scope(s)
  { }
  ~PendingBlock() {
    if (scope) delete scope;
  }
};



enum ReducerMode {
  RM_Reduce,    // Rewrite expression to an equivalent value.
  RM_Promote    // Abstract interpret expression to find bounding type.
};



/// CFGReducer will rewrite a high-level ohmu AST to a CFG.
class CFGReducer : public CopyReducer, public LazyCopyTraversal<CFGReducer> {
public:
  typedef LazyCopyTraversal<CFGReducer> SuperTv;

  BasicBlock* currentContinuation()   { return currentContinuation_; }
  void setContinuation(BasicBlock *b) { currentContinuation_ = b;    }

  Function* reduceFunction(Function &orig, VarDecl *nvd, SExpr* e0);
  Code*     reduceCode(Code &orig, SExpr* e0, SExpr* e1);
  Field*    reduceField(Field &orig, SExpr* e0, SExpr* e1);
  Record*   reduceRecordEnd(Record *res);

  SExpr*    reduceIdentifier(Identifier &orig);
  SExpr*    reduceVariable(Variable &orig, VarDecl* vd);
  SExpr*    reduceProject(Project &orig, SExpr* e);
  SExpr*    reduceApply(Apply &orig, SExpr* e, SExpr* a);
  SExpr*    reduceCall(Call &orig, SExpr* e);
  SExpr*    reduceLoad(Load &orig, SExpr* e);
  SExpr*    reduceUnaryOp(UnaryOp &orig, SExpr* e0);
  SExpr*    reduceBinaryOp(BinaryOp &orig, SExpr* e0, SExpr* e1);

  template <class T>
  MAPTYPE(SExprReducerMap, T) traverse(T* e, TraversalKind k);

  // Code traversals will rewrite the code blocks to SCFGs.
  SExpr* traverseCode(Code* e, TraversalKind k);

  // Eliminate let expressions.
  SExpr* traverseLet(Let* e, TraversalKind k);

  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *e, TraversalKind k);

  SCFG* beginCFG(SCFG *Cfg, unsigned NBlocks=0, unsigned NInstrs=0) override;
  void  endCFG() override;

  /// Lower e by building CFGs for all code blocks.
  static SExpr* lower(SExpr *e, MemRegionRef a);


protected:
  ReducerMode switchMode(ReducerMode m) {
    auto om = mode_;
    mode_ = m;
    return om;
  }

  void restoreMode(ReducerMode m) { mode_ = m; }

  SExpr* calculateResidualType(SExpr* res, SExpr* e);

  /// Inline a call to a function defined inside the current CFG.
  SExpr* inlineLocalCall(PendingBlock *pb, Code *c);

  /// Do automatic type conversions for arithmetic operations.
  bool checkAndExtendTypes(Instruction*& i0, Instruction*& i1);

  /// Implement lazy block traversal.
  void traversePendingBlocks();

public:
  CFGReducer(MemRegionRef a)
     : CopyReducer(a), mode_(RM_Reduce), currentContinuation_(nullptr) {}
  ~CFGReducer() { }

private:
  ReducerMode  mode_;
  BasicBlock*  currentContinuation_;    //< continuation for current block.
  NestedStack<SExpr*> resultArgs_;      //< unapplied arguments on curr path.
  BoundingType        resultType_;      //< type bound for current path.

  std::vector<std::unique_ptr<PendingBlock>> pendingBlocks_;
  std::queue<PendingBlock*>                  pendingBlockQueue_;
  DenseMap<Code*, PendingBlock*>             codeMap_;
};



template <class T>
MAPTYPE(SExprReducerMap, T) CFGReducer::traverse(T* e, TraversalKind k) {
  // Save pending arguments and resultType.
  unsigned     argSave;
  BoundingType resultTypeSave;
  if (k != TRV_Path) {
    argSave        = resultArgs_.save();
    resultTypeSave = resultType_;
  }

  // Save the current continuation.
  BasicBlock* cont = currentContinuation();
  if (k != TRV_Tail)
    setContinuation(nullptr);

  // Clear the result type; the traversal will write a result here.
  resultType_.clear();

  // Do the traversal.  (This will set resultType_ and may add pending args).
  auto* result = SuperTv::traverse(e, k);

  // diag.warning("trace: ") << e << " --> " << result << " : " << resultType_.typeExpr();

  // Restore the continuation.
  setContinuation(cont);

  // Restore old pending arguments and resultType.
  // (Discard the ones we just calculated.)
  if (k != TRV_Path) {
    resultArgs_.clear();
    resultArgs_.restore(argSave);
    resultType_ = resultTypeSave;
  }

  if (!currentBB())
    return result;

  // If we have a continuation, then jump to it.
  if (cont && k == TRV_Tail) {
    newGoto(cont, result);
    return nullptr;
  }
  return result;
}


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_CFGREDUCER_H

