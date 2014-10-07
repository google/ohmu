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


class TILDebugPrinter : public PrettyPrinter<TILDebugPrinter, std::ostream> {
public:
  TILDebugPrinter() : PrettyPrinter(true, false, false) { }
};



class VarContext {
public:
  VarContext() { }

  VarDecl*&   operator[](unsigned i) {
    assert(i < size() && "Array out of bounds.");
    return vars_[size()-1-i];
  }

  VarDecl*    lookup(StringRef s);
  size_t      size() const      { return vars_.size(); }
  void        push(VarDecl *vd) { vars_.push_back(vd); }
  void        pop()             { vars_.pop_back(); }
  VarDecl*    back()            { return vars_.back(); }
  VarContext* clone()           { return new VarContext(*this); }

private:
  VarContext(const VarContext& ctx) : vars_(ctx.vars_) { }

  std::vector<VarDecl*> vars_;
};



class CFGRewriteReducer : public CopyReducer {
public:
  BasicBlock* currentContinuation() {
    return continuationStack_.back();
  }

  void pushContinuation(BasicBlock* b) {
    continuationStack_.push_back(b);
  }

  BasicBlock* popContinuation() {
    BasicBlock* b = continuationStack_.back();
    continuationStack_.pop_back();
    return b;
  }


  bool enterSubExpr(SExpr *e, TraversalKind k) {
    if (k == TRV_Lazy)  // Skip lazy terms -- we'll handle them specially.
      return false;

    if (k == TRV_Tail) {
      pushContinuation(currentContinuation());
    } else {
      pushContinuation(nullptr);
    }
    return true;
  }

  template <class T>
  T* exitSubExpr(SExpr *e, T* res, TraversalKind k) {
    BasicBlock *b = popContinuation();
    if (!currentBB_)
      return res;

    addInstruction(res);
    // If we have a continuation, then jump to it.
    if (b) {
      assert(k == TRV_Tail);
      createGoto(b, res);
      return nullptr;
    }
    return res;
  }

  std::nullptr_t skipTraverse(SExpr *E) { return nullptr; }


  void enterScope(VarDecl *orig, VarDecl *Nv);
  void exitScope(const VarDecl *orig);

  void enterBasicBlock(BasicBlock *bb, BasicBlock *nbb) { }
  void exitBasicBlock (BasicBlock *bb) { }

  void enterCFG(SCFG *cfg, SCFG* ncfg) { }
  void exitCFG (SCFG *cfg) { }


  SExpr* reduceApply(Apply &orig, SExpr* e, SExpr *a);
  SExpr* reduceCall(Call &orig, SExpr *e);
  SExpr* reduceCode(Code& orig, SExpr* e0, SExpr* e1);
  SExpr* reduceIdentifier(Identifier &orig);
  SExpr* reduceLet(Let &orig, VarDecl *nvd, SExpr *b);


  // Create a new basic block.
  BasicBlock* addBlock(unsigned nargs = 0);

  /// Add BB to the current CFG, and start working on it.
  void startBlock(BasicBlock *bb);

  /// Terminate the current block with a branch instruction.
  /// This will create new blocks for the branches.
  Branch* createBranch(SExpr *cond);

  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *target, SExpr* result);

  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *target, std::vector<SExpr*>& args);

  /// Creates a new CFG.
  /// Returns the exit block, for use as a continuation.
  void initCFG();

  /// Finish lazy traversals.
  template<class V>
  void finishLazyBlocks(V& visitor);

  /// Completes the CFG and returns it.
  SCFG* finishCFG();

protected:
  struct PendingBlock {
    SExpr*      expr;
    BasicBlock* block;
    BasicBlock* continuation;
    std::unique_ptr<VarContext> ctx;

    PendingBlock(SExpr *e, BasicBlock *b, VarContext* c)
      : expr(e), block(b), continuation(nullptr), ctx(c)
    { }
  };

  // Add a new instruction to the current basic block.
  void addInstruction(SExpr* e);

  // Finish the current basic block, terminating it with Term.
  void finishBlock(Terminator* term);

public:
  CFGRewriteReducer(MemRegionRef a)
      : CopyReducer(a), varCtx_(new VarContext()),
        currentCFG_(nullptr), currentBB_(nullptr),
        currentInstrNum_(0), currentBlockNum_(2) { }

private:
  friend class CFGRewriter;

  std::unique_ptr<VarContext> varCtx_;
  std::vector<SExpr*> instructionMap_;
  std::vector<SExpr*> blockMap_;

  SCFG*       currentCFG_;                       //< the current SCFG
  BasicBlock* currentBB_;                        //< the current basic block
  unsigned    currentInstrNum_;
  unsigned    currentBlockNum_;

  std::vector<Phi*>         currentArgs_;        //< arguments in currentBB.
  std::vector<Instruction*> currentInstrs_;      //< instructions in currentBB.

  std::vector<BasicBlock*>  continuationStack_;
  std::vector<SExpr*>       pendingPathArgs_;

  DenseMap<Code*, unsigned> codeMap_;
  std::vector<PendingBlock> pendingBlocks_;
  std::queue<unsigned>      pendingBlockQueue_;
};



class CFGRewriter : public Traversal<CFGRewriter, CFGRewriteReducer> {
public:
  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *e, CFGRewriteReducer *r,
                            TraversalKind k);

  static SCFG* convertSExprToCFG(SExpr *e, MemRegionRef a);
};




template<class V>
void CFGRewriteReducer::finishLazyBlocks(V& visitor) {
  while (!pendingBlockQueue_.empty()) {
    unsigned pi = pendingBlockQueue_.front();
    pendingBlockQueue_.pop();
    PendingBlock& pb = pendingBlocks_[pi];
    if (!pb.continuation)
      continue;   // unreachable or already processed block.

    pushContinuation(pb.continuation);
    startBlock(pb.block);
    varCtx_ = std::move(pb.ctx);
    visitor.traverse(pb.expr, this, TRV_Tail);
    popContinuation();

    pb.continuation = nullptr;  // mark block as processed.
  }
}


}  // end namespace ohmu

#endif  // OHMU_TIL_CFGREDUCER_H

