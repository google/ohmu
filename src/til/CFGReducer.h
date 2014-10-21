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
  std::unique_ptr<VarContext> ctx;
  bool processed;

  PendingBlock(SExpr *e, BasicBlock *b, VarContext* c)
    : expr(e), block(b), continuation(nullptr), ctx(c), processed(false)
  { }
};


class CFGReducer : public CopyReducer,
                   public Traversal<CFGReducer, SExprReducerMap> {
public:
  typedef Traversal<CFGReducer, SExprReducerMap> SuperTv;

  BasicBlock* currentContinuation()   { return currentContinuation_; }
  void setContinuation(BasicBlock *b) { currentContinuation_ = b;    }

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

  SCFG* beginSCFG(SCFG *Cfg, unsigned NBlocks=0, unsigned NInstrs=0) override;
  void  endSCFG() override;

  static SExpr* lower(SExpr *e, MemRegionRef a);


protected:
  unsigned numPendingArgs() {
    return pendingPathArgs_.size() - pendingPathArgLen_;
  }
  unsigned savePendingArgs() {
    unsigned plen = pendingPathArgLen_;
    pendingPathArgLen_ = pendingPathArgs_.size();
    return plen;
  }
  void restorePendingArgs(unsigned plen) {
    pendingPathArgLen_ = plen;
  }

  // Implement lazy block traversal.
  void traversePendingBlocks();

public:
  CFGReducer(MemRegionRef a)
      : CopyReducer(a), currentContinuation_(nullptr), pendingPathArgLen_(0)
  { }
  ~CFGReducer() { }

private:
  BasicBlock* currentContinuation_;      //< continuation for current block.
  unsigned    pendingPathArgLen_;

  std::vector<SExpr*>        pendingPathArgs_;
  DenseMap<Code*, unsigned>  codeMap_;
  std::vector<PendingBlock>  pendingBlocks_;
  std::queue<unsigned>       pendingBlockQueue_;
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

