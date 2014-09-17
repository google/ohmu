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

#ifndef OHMU_CFG_REDUCER_H
#define OHMU_CFG_REDUCER_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

#include <vector>

namespace ohmu {

using namespace clang::threadSafety::til;


class TILDebugPrinter : public PrettyPrinter<TILDebugPrinter, std::ostream> {
public:
  TILDebugPrinter() : PrettyPrinter(false, false) { }
};



class VarContext {
public:
  VarContext() { }

  SExpr* lookup(StringRef S) {
    for (unsigned i=0,n=Vars.size(); i < n; ++i) {
      VarDecl* V = Vars[n-i-1];
      if (V->name() == S) {
        return V;
      }
    }
    return nullptr;
  }

  void        push(VarDecl *V) { Vars.push_back(V); }
  void        pop()            { Vars.pop_back(); }
  VarDecl*    back()           { return Vars.back(); }
  VarContext* clone()          { return new VarContext(Vars); }

private:
  VarContext(const std::vector<VarDecl*>& Vs) : Vars(Vs) { }

  std::vector<VarDecl*> Vars;
};



class CFGRewriteReducer : public CopyReducerBase {
public:
  /// A Context consists of the reducer, and the current continuation.
  class ContextT : public DefaultContext<CFGRewriteReducer> {
  public:
    ContextT(CFGRewriteReducer *R, BasicBlock *C)
      : DefaultContext(R), Continuation(C)
    { }

    ContextT sub(TraversalKind K) const {
      if (K == TRV_Tail)
        return *this;
      else
        return ContextT(get(), nullptr);
    }

    ContextT getCurrentContinuation() {
      if (Continuation)
        return *this;
      else
        return ContextT(get(), get()->makeContinuation());
    }

    bool insideCFG() { return get()->currentBB_; }

    BasicBlock* continuation() { return Continuation; }

  private:
    friend class CFGRewriteReducer;
    BasicBlock* Continuation;
  };


  void enterScope(VarDecl *Orig, VarDecl *Nv) {
    if (Orig->name().length() > 0) {
      varCtx_.push(Nv);
      if (currentBB_)
        addLetDecl(Nv);
    }
  }

  void exitScope(const VarDecl *Orig) {
    if (Orig->name().length() > 0) {
      assert(Orig->name() == varCtx_.back()->name() && "Variable mismatch");
      varCtx_.pop();
    }
  }

  void enterBasicBlock(BasicBlock *BB, BasicBlock *Nbb) { }
  void exitBasicBlock (BasicBlock *BB) { }

  void enterCFG(SCFG *Cfg, SCFG* NCfg) { }
  void exitCFG (SCFG *Cfg) { }


  SExpr* reduceIdentifier(Identifier &Orig) {
    SExpr* E = varCtx_.lookup(Orig.name());
    // TODO: emit warning on name-not-found.
    if (E)
      return E;
    return new (Arena) Identifier(Orig);
  }

  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr *B) {
    if (currentCFG_)
      return B;   // eliminate the let
    else
      return new (Arena) Let(Orig, Nvd, B);
  }


  /// Process the result of a traversal.
  SExpr* processResult(SExpr& Orig, SExpr* Result, ContextT Ctx) {
    if (!currentBB_)     // no current block
      return Result;
    addInstruction(Result);

    if (Ctx.continuation()) {
      // If we have a continuation, then terminate current block,
      // and pass the result to the continuation.
      createGoto(Ctx.continuation(), Result);
      return nullptr;
    }
    return Result;
  }


  /// Add BB to the current CFG, and start working on it.
  void startBlock(BasicBlock *BB) {
    assert(currentBB_ == nullptr && "Haven't finished current block.");
    assert(currentArgs_.empty());
    assert(currentInstrs_.empty());
    assert(BB->instructions().size() == 0 && "Already processed block.");

    currentBB_ = BB;
    if (!BB->cfg())
      currentCFG_->add(BB);
  }


  /// Terminate the current block with a branch instruction.
  /// This will create new blocks for the branches.
  Branch* createBranch(SExpr *Cond) {
    assert(currentBB_);

    // Create new basic blocks for then and else.
    BasicBlock *Ntb = new (Arena) BasicBlock(Arena);
    BasicBlock *Neb = new (Arena) BasicBlock(Arena);
    Ntb->addPredecessor(currentBB_);
    Neb->addPredecessor(currentBB_);

    // Terminate current basic block with a branch
    auto *Nt = new (Arena) Branch(Cond, Ntb, Neb);
    finishBlock(Nt);
    return Nt;
  }


  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *Target, SExpr* Result) {
    assert(currentBB_);

    unsigned Idx = Target->addPredecessor(currentBB_);
    if (Target->arguments().size() > 0) {
      // First argument is always the result
      SExpr *E = Target->arguments()[0];
      if (Phi *Ph = dyn_cast<Phi>(E))
        Ph->values()[Idx] = Result;
    }
    auto *Nt = new (Arena) Goto(Target, Idx);
    finishBlock(Nt);
    return Nt;
  }


  /// Creates a new CFG.
  /// Returns the exit block, for use as a continuation.
  BasicBlock* initCFG() {
    assert(currentCFG_ == nullptr && currentBB_ == nullptr);
    currentCFG_ = new (Arena) SCFG(Arena, 0);
    currentBB_ = currentCFG_->entry();
    assert(currentBB_->instructions().size() == 0);
    return currentCFG_->exit();
  }

  /// Completes the CFG and returns it.
  SCFG* finishCFG() {
    currentCFG_->computeNormalForm();
    return currentCFG_;
  }


protected:
  // Add new let variable to the current basic block.
  void addLetDecl(VarDecl* Nv) {
    // Set the block and ID now, to mark it as having been added.
    Nv->setID(currentBB_, currentInstrNum_++);
    if (currentInstrs_.size() > 0 &&
        currentInstrs_.back() == Nv->definition() &&
        !isa<VarDecl>(Nv->definition())) {
      // Definition already in block -- replace old instr with let decl.
      Nv->definition()->setID(nullptr, 0);
      currentInstrs_.back() = Nv;
    }
    else {
      currentInstrs_.push_back(Nv);
    }
  }

  // Add a new instruction to the current basic block.
  void addInstruction(SExpr* E) {
    if (!ThreadSafetyTIL::isTrivial(E) && !E->block()) {
      // Set the block and ID now, to mark it as having been added.
      // We won't actually add instructions until the block is done.
      E->setID(currentBB_, currentInstrNum_++);
      currentInstrs_.push_back(E);
    }
  }

  // Finish the current basic block, terminating it with Term.
  void finishBlock(Terminator* Term) {
    assert(currentBB_);
    assert(currentBB_->instructions().size() == 0);

    currentBB_->instructions().reserve(currentInstrs_.size(), Arena);
    for (auto *E : currentInstrs_) {
      currentBB_->addInstruction(E);
    }
    currentBB_->setTerminator(Term);
    currentArgs_.clear();
    currentInstrs_.clear();
    currentBB_ = nullptr;
  }

  // Make a new continuation
  BasicBlock* makeContinuation() {
    auto *Ncb = new (Arena) BasicBlock(Arena);
    auto *Nph = new (Arena) Phi();
    Ncb->addArgument(Nph);
    return Ncb;
  }


public:
  CFGRewriteReducer(MemRegionRef A)
    : CopyReducerBase(A), currentCFG_(nullptr), currentBB_(nullptr),
      currentInstrNum_(0)
  { }


private:
  friend class ContextT;

  VarContext varCtx_;
  std::vector<SExpr*> instructionMap_;
  std::vector<SExpr*> blockMap_;

  SCFG*       currentCFG_;              // the current SCFG
  BasicBlock* currentBB_;               // the current basic block
  unsigned    currentInstrNum_;
  std::vector<SExpr*> currentArgs_;     // arguments in currentBB.
  std::vector<SExpr*> currentInstrs_;   // instructions in currentBB.
};



class CFGRewriter : public Traversal<CFGRewriter, CFGRewriteReducer> {
public:
  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *E, CtxT Ctx,
                            TraversalKind K = TRV_Normal) {
    if (!Ctx.insideCFG()) {
      // Just do a normal traversal if we're not currently rewriting in a CFG.
      return E->traverse(*this->self(), Ctx);
    }

    // Get the current continuation, or make one.
    CtxT Cont = Ctx.getCurrentContinuation();

    // End current block with a branch
    SExpr*  Nc = this->self()->traverse(E->condition(), Ctx);
    Branch* Br = Ctx->createBranch(Nc);

    // Process the then and else blocks
    Cont->startBlock(Br->thenBlock());
    this->self()->traverse(E->thenExpr(), Cont, TRV_Tail);

    Cont->startBlock(Br->elseBlock());
    this->self()->traverse(E->elseExpr(), Cont, TRV_Tail);

    // If we had an existing continuation, then we're done.
    // The then/else blocks will call the continuation.
    if (Ctx.continuation())
      return nullptr;

    // Otherwise, if we created a new continuation, then start processing it.
    Cont->startBlock(Cont.continuation());
    assert(Cont.continuation()->arguments().size() > 0);
    return Cont.continuation()->arguments()[0];
  }


  static SCFG* convertSExprToCFG(SExpr *E, MemRegionRef A) {
    CFGRewriteReducer Reducer(A);
    CFGRewriter Traverser;

    auto *Exit = Reducer.initCFG();
    Traverser.traverse(E, CtxT(&Reducer, Exit), TRV_Tail);
    return Reducer.finishCFG();
  }
};



}  // end namespace ohmu

#endif  // OHMU_CFG_REDUCER_H

