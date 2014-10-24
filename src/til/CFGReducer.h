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

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"
#include "til/CopyReducer.h"

#include <cstddef>
#include <memory>
#include <queue>
#include <vector>

namespace ohmu {

using namespace clang::threadSafety::til;


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
  typedef Traversal<CFGReducer, SExprReducerMap> SuperTv;

  BasicBlock* currentContinuation()   { return currentContinuation_; }
  void setContinuation(BasicBlock *b) { currentContinuation_ = b;    }

  SExpr* reduceProject(Project &orig, SExpr* e);
  SExpr* reduceApply(Apply &orig, SExpr* e, SExpr *a);
  SExpr* reduceCall(Call &orig, SExpr *e);
  SExpr* reduceCode(Code& orig, SExpr* e0, SExpr* e1);
  SExpr* reduceIdentifier(Identifier &orig);
  SExpr* reduceLet(Let &orig, VarDecl *nvd, SExpr *b);

  template <class T>
  MAPTYPE(SExprReducerMap, T) traverse(T* e, TraversalKind k);

  // Code traversals will rewrite the code blocks to SCFGs.
  SExpr* traverseCode(Code* e, TraversalKind k);

  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *e, TraversalKind k);

  SCFG* beginCFG(SCFG *Cfg, unsigned NBlocks=0, unsigned NInstrs=0) override;
  void  endCFG() override;

  /// Lower e by building CFGs for all code blocks.
  static SExpr* lower(SExpr *e, MemRegionRef a);


protected:
  /// Return the number of pending function arguments.
  /// Function call arguments are saved in a pending list until a Call expr.
  unsigned numPendingArgs() {
    return pendingPathArgs_.size() - pendingPathArgLen_;
  }

  /// Return the pending arguments.
  ArrayRef<SExpr*> pendingArgs() {
    unsigned nargs = numPendingArgs();
    SExpr** pbegin = &pendingPathArgs_[pendingPathArgs_.size()-nargs];
    return ArrayRef<SExpr*>(pbegin, nargs);
  }

  /// Clear all pending arguments.
  void clearPendingArgs() {
    unsigned nargs = numPendingArgs();
    for (unsigned i = 0; i < nargs; ++i)
      pendingPathArgs_.pop_back();
  }

  /// Save pending arguments on a stack.
  /// Return a value that can be passed to restorePendingArgs later.
  unsigned savePendingArgs() {
    unsigned plen = pendingPathArgLen_;
    pendingPathArgLen_ = pendingPathArgs_.size();
    return plen;
  }

  /// Restore a list of previously saved arguments.
  void restorePendingArgs(unsigned plen) {
    pendingPathArgLen_ = plen;
  }

  /// Implement lazy block traversal.
  void traversePendingBlocks();

public:
  CFGReducer(MemRegionRef a)
      : CopyReducer(a), currentContinuation_(nullptr), pendingPathArgLen_(0)
  { }
  ~CFGReducer() { }

private:
  BasicBlock*  currentContinuation_;      //< continuation for current block.
  unsigned     pendingPathArgLen_;
  std::vector<SExpr*> pendingPathArgs_;

  std::vector<std::unique_ptr<PendingBlock>> pendingBlocks_;
  std::queue<PendingBlock*>                  pendingBlockQueue_;
  DenseMap<Code*, PendingBlock*>             codeMap_;
};



template <class T>
MAPTYPE(SExprReducerMap, T) CFGReducer::traverse(T* e, TraversalKind k) {
  unsigned plen = savePendingArgs();
  // This is a CPS transform, so we track the current continuation.
  BasicBlock* cont = currentContinuation();
  if (k != TRV_Tail)
    setContinuation(nullptr);

  // Do the traversal
  auto* result = SuperTv::traverse(e, k);

  // Restore continuation.
  setContinuation(cont);
  // Restore pending arguments, and ensure the traversal didn't add any.
  if (k != TRV_Path) {
    assert(numPendingArgs() == 0 && "Unhandled arguments.");
    restorePendingArgs(plen);
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


}  // end namespace ohmu

#endif  // OHMU_TIL_CFGREDUCER_H

