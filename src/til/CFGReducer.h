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


struct PendingBlock {
  SExpr*      expr;
  BasicBlock* block;
  BasicBlock* continuation;
  std::unique_ptr<ScopeFrame> scope;

  PendingBlock(SExpr *e, BasicBlock *b, ScopeFrame* s)
    : expr(e), block(b), continuation(nullptr), scope(s)
  { }
};


class CFGReducer : public CopyReducer, public LazyCopyTraversal<CFGReducer> {
public:
  typedef LazyCopyTraversal<CFGReducer> SuperTv;

  BasicBlock* currentContinuation()   { return currentContinuation_; }
  void setContinuation(BasicBlock *b) { currentContinuation_ = b;    }

  SExpr* reduceIdentifier(Identifier &orig);
  SExpr* reduceVariable(Variable &orig, VarDecl* vd);
  SExpr* reduceProject(Project &orig, SExpr* e);
  SExpr* reduceApply(Apply &orig, SExpr* e, SExpr* a);
  SExpr* reduceCall(Call &orig, SExpr* e);
  SExpr* reduceLoad(Load &orig, SExpr* e);
  SExpr* reduceUnaryOp(UnaryOp &orig, SExpr* e0);
  SExpr* reduceBinaryOp(BinaryOp &orig, SExpr* e0, SExpr* e1);
  SExpr* reduceCode(Code& orig, SExpr* e0, SExpr* e1);

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
  /// Inline a call to a function defined inside the current CFG.
  SExpr* inlineLocalCall(PendingBlock *pb, Code *c);

  /// Do automatic type conversions for arithmetic operations.
  bool checkAndExtendTypes(Instruction*& i0, Instruction*& i1);

  /// Implement lazy block traversal.
  void traversePendingBlocks();

private:
  void setResidualBoundingType(Instruction* res, SExpr* typ,
                               BoundingType::Relation rel);

public:
  CFGReducer(MemRegionRef a) : CopyReducer(a), currentContinuation_(nullptr) {}
  ~CFGReducer() { }

private:
  BasicBlock*  currentContinuation_;    //< continuation for current block.
  NestedStack<SExpr*> pendingArgs_;     //< unapplied arguments on curr path.

  std::vector<std::unique_ptr<PendingBlock>> pendingBlocks_;
  std::queue<PendingBlock*>                  pendingBlockQueue_;
  DenseMap<Code*, PendingBlock*>             codeMap_;
};



template <class T>
MAPTYPE(SExprReducerMap, T) CFGReducer::traverse(T* e, TraversalKind k) {
  // Save pending arguments, if we're not on the spine of a path.
  unsigned argsave = 0;
  if (k != TRV_Path)
    argsave = pendingArgs_.save();

  // Save the currentcontinuation.  (This is a CPS transform.)
  BasicBlock* cont = currentContinuation();
  if (k != TRV_Tail)
    setContinuation(nullptr);

  // Do the traversal
  auto* result = SuperTv::traverse(e, k);

  // Restore the continuation.
  setContinuation(cont);
  // Restore pending arguments, and ensure the traversal didn't add any.
  if (k != TRV_Path)
    pendingArgs_.restore(argsave);

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

