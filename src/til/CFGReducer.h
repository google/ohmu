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
  VarContext* clone()          { return new VarContext(Vars); }

private:
  VarContext(const std::vector<VarDecl*>& Vs) : Vars(Vs) { }

  std::vector<VarDecl*> Vars;
};


class CFGContext {
public:
  // Create a new variable from orig, and push it onto the lexical scope.
  VarDecl *enterScope(VarDecl &Orig, R_SExpr E0) {
    VarDecl *Nv = new (Arena) VarDecl(Orig, E0);
    if (Orig.name().length() > 0) {
      varCtx_.push(Nv);
      if (currentBB_) {
        if (currentInstrs_.size() > 0 && currentInstrs_.back() == E0)
          currentInstrs_.back() = Nv;
        else
          currentInstrs_.push_back(Nv);
      }
    }
    return Nv;
  }

  // Exit the lexical scope of orig.
  void exitScope(const VarDecl &Orig) {
    if (Orig.name().length() > 0)
      varCtx_.pop();
  }

  void enterCFG(SCFG &Cfg) {}
  void exitCFG(SCFG &Cfg) {}
  void enterBasicBlock(BasicBlock &BB) {}
  void exitBasicBlock(BasicBlock &BB) {}

  void enterBasicBlock(BasicBlock *BB, BasicBlock *Nbb) {}
  void exitBasicBlock (BasicBlock *BB) {}

  void enterCFG(SCFG *Cfg, SCFG* NCfg) {}
  void exitCFG (SCFG *Cfg) {}

private:
};



class CFGRewriter : public Traversal<CFGRewriter, CFGContext, CopyReducer>,
                    public CopyReducer {
public:
  SCFG* convertToCFG(SExpr *E) {
    assert(currentCFG_ == nullptr && currentBB_ == nullptr);
    currentCFG_ = new (Arena) SCFG(Arena, 0);
    currentBB_ = currentCFG_->entry();
    traverseSExpr(E, );
    currentCFG_->computeNormalForm();
    return currentCFG_;
  }

  // Lower SExpr, writing intermediate results to the current basic block.
  // If Ctx.Continuation is true, then terminate the basic block, by passing
  // the result to the continuation.
  R_SExpr traverseSExpr(SExpr *E, R_Ctx Ctx) {
    R_SExpr Result = this->self()->traverseByCase(E, Ctx);
    if (!Ctx.Continuation)
      return Result;  // No continuation.  Continue with current basic block.
    if (!currentBB_)
      return Result;  // No current basic block.  Rewrite expressions in place.
    terminateWithGoto(Result, Ctx.Continuation);
    return nullptr;
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    SExpr* E = varCtx_.lookup(Orig.name());
    // TODO: emit warning on name-not-found.
    if (E)
      return E;
    return new (Arena) Identifier(Orig);
  }

  R_SExpr reduceLet(Let &Orig, VarDecl *Nvd, R_SExpr B) {
    if (currentCFG_)
      return B;   // eliminate the let
    else
      return new (Arena) Let(Orig, Nvd, B);
  }


  // We have to trap IfThenElse on the traverse rather than reduce, since it
  // the then/else expressions must be evaluated in different basic blocks.
  R_SExpr traverseIfThenElse(IfThenElse *E, Context Ctx) {
    if (!currentBB_) {
      // Just do a normal traversal if we're not currently rewriting in a CFG.
      return E->traverse(*this->self(), Ctx);
    }

    SExpr* Nc = this->self()->traverseSExpr(E->condition(), subExprCtx(Ctx));

    // Create new basic blocks for then and else.
    BasicBlock *Ntb = new (Arena) BasicBlock(Arena);
    BasicBlock *Neb = new (Arena) BasicBlock(Arena);

    // Create a continuation if we don't already have one.
    BasicBlock *Ncb = Ctx.Continuation;
    Phi *NcbArg = nullptr;
    if (!Ncb) {
      Ncb = new (Arena) BasicBlock(Arena);
      NcbArg = new (Arena) Phi();
      Ncb->addArgument(NcbArg);
    }

    // Terminate current basic block with a branch
    Ntb->addPredecessor(currentBB_);
    Neb->addPredecessor(currentBB_);
    auto *Nt = new (Arena) Branch(Nc, Ntb, Neb);
    terminateCurrentBB(Nt);

    // Rewrite then and else in new blocks
    startBB(Ntb);
    this->self()->traverseSExpr(E->thenExpr(), Context(TRV_Normal, Ncb));

    startBB(Neb);
    this->self()->traverseSExpr(E->elseExpr(), Context(TRV_Normal, Ncb));

    if (Ctx.Continuation)
      return nullptr;

    // Jump to the newly created continuation
    startBB(Ncb);
    return NcbArg;
  }


protected:
  // Start a new basic block, and traverse E.
  void startBB(BasicBlock *BB) {
    assert(currentBB_ == nullptr);
    assert(currentArgs_.empty());
    assert(currentInstrs_.empty());

    currentBB_ = BB;
    currentCFG_->add(BB);
  }

  // Finish the current basic block, terminating it with Term.
  void terminateCurrentBB(Terminator* Term) {
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

  // If the current basic block exists, terminate it with a goto to the
  // target continuation.  Result is passed as an argument to the continuation.
  void terminateWithGoto(SExpr* Result, BasicBlock *Target) {
    assert(currentBB_);
    assert(Target->arguments().size() > 0);

    unsigned Idx = Target->addPredecessor(currentBB_);
    SExpr *E = Target->arguments()[0];
    if (Phi *Ph = dyn_cast<Phi>(E)) {
      Ph->values()[Idx] = Result;
    }

    auto *Term = new (Arena) Goto(Target, Idx);
    terminateCurrentBB(Term);
  }

  CFGReducer(MemRegionRef A)
     : CFGReducerBase(A), currentCFG_(nullptr), currentBB_(nullptr) {}

protected:
  VarContext varCtx_;
  DenseMap<SExpr*, SExpr*> varMap_;

  SCFG*       currentCFG_;              // the current SCFG
  BasicBlock* currentBB_;               // the current basic block
  std::vector<SExpr*> currentArgs_;     // arguments in currentBB.
  std::vector<SExpr*> currentInstrs_;   // instructions in currentBB.
};


class CFGLoweringPass : public CFGReducer<CFGLoweringPass> {
public:
  CFGLoweringPass(MemRegionRef A) : CFGReducer(A) {}

  static SCFG* convertSExprToCFG(SExpr *E, MemRegionRef A) {
    CFGLoweringPass L(A);
    return L.convertToCFG(E);
  }
};


}  // end namespace ohmu

#endif  // OHMU_CFG_REDUCER_H

