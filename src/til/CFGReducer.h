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


struct PendingBlock {
  SExpr*      expr;
  BasicBlock* block;
  BasicBlock* continuation;
  std::unique_ptr<VarContext> ctx;

  PendingBlock(SExpr *e, BasicBlock *b, VarContext* c)
    : expr(e), block(b), continuation(nullptr), ctx(c)
  { }
};



class CFGRewriteReducer : public CopyReducer {
public:
  BasicBlock* currentContinuation()   { return currentContinuation_; }
  void setContinuation(BasicBlock *b) { currentContinuation_ = b;    }

  unsigned currentPathLen() { return currentPathArgLen_; }
  void setPathLen(unsigned i) { currentPathArgLen_ = i; }
  void clearPathLen() { currentPathArgLen_ = pendingPathArgs_.size(); }

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


  // Add a new instruction to the current basic block.
  void addInstruction(SExpr* e);

  // Create a new basic block.
  BasicBlock* addBlock(unsigned nargs = 0);

  /// Add BB to the current CFG, and start working on it.
  void startBlock(BasicBlock *bb);

  // Finish the current basic block, terminating it with Term.
  void finishBlock(Terminator* term);

  /// Terminate the current block with a branch instruction.
  /// This will create new blocks for the branches.
  Branch* createBranch(SExpr *cond);

  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *target, SExpr* result);

  /// Terminate the current block with a Goto instruction.
  /// Takes the top len arguments from args.
  Goto* createGoto(BasicBlock *target, std::vector<SExpr*>& args, unsigned len);

  /// Creates a new CFG.
  /// Returns the exit block, for use as a continuation.
  void initCFG();

  /// Completes the CFG and returns it.
  SCFG* finishCFG();

public:
  CFGRewriteReducer(MemRegionRef a)
      : CopyReducer(a), varCtx_(new VarContext()),
        currentCFG_(nullptr), currentBB_(nullptr),
        currentContinuation_(nullptr), currentPathArgLen_(0)
  { }

private:
  friend class CFGRewriter;

  std::unique_ptr<VarContext> varCtx_;
  std::vector<SExpr*> instructionMap_;
  std::vector<SExpr*> blockMap_;

  SCFG*       currentCFG_;                      //< the current SCFG
  BasicBlock* currentBB_;                       //< the current basic block
  BasicBlock* currentContinuation_;      //< continuation for current block.
  unsigned    currentPathArgLen_;

  std::vector<Phi*>          currentArgs_;      //< arguments in currentBB.
  std::vector<Instruction*>  currentInstrs_;    //< instructions in currentBB.
  std::vector<SExpr*>        pendingPathArgs_;
  DenseMap<Code*, unsigned>  codeMap_;
  std::vector<PendingBlock>  pendingBlocks_;
  std::queue<unsigned>       pendingBlockQueue_;
};



class CFGRewriter : public Traversal<CFGRewriter, CFGRewriteReducer> {
public:
  typedef Traversal<CFGRewriter, CFGRewriteReducer> Super;

  template <class T>
  MAPTYPE(CFGRewriteReducer, T)
  traverse(T* e, CFGRewriteReducer *r, TraversalKind k) {
    if (k == TRV_Lazy)  // Skip lazy terms -- we'll handle them specially.
      return nullptr;

    // This is a CPS transform, so we track the current continuation.
    BasicBlock* cont = r->currentContinuation();
    if (k != TRV_Tail)
      r->setContinuation(nullptr);

    // Save any pending arguments
    // unsigned plen = r->currentPathLen();
    // r->clearPathLen();

    // Do the traversal
    auto* result = Super::traverse(e, r, k);

    // Restore pending arguments, and ensure the traversal didn't add any.
    // if (k != TRV_Path) {
    //   r->setPathLen(plen);
    //  assert(plen == r->pendingPathArgs_.size() && "Unhandled arguments.");
    // }
    // Restore continuation.
    r->setContinuation(cont);

    if (!r->currentBB_)
      return result;

    // Add instructions to the current basic block
    r->addInstruction(result);

    // If we have a continuation, then jump to it.
    if (cont && k == TRV_Tail) {
      r->createGoto(cont, result);
      return nullptr;
    }
    return result;
  }

  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *e, CFGRewriteReducer *r,
                            TraversalKind k);

  // Implement lazy block traversal.
  void traversePendingBlocks(CFGRewriteReducer *r);

  static SCFG* convertSExprToCFG(SExpr *e, MemRegionRef a);
};


}  // end namespace ohmu

#endif  // OHMU_TIL_CFGREDUCER_H

