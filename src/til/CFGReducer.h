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

  SExpr* lookup(StringRef S);

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
  class ContextT : public DefaultContext<ContextT, CFGRewriteReducer> {
  public:
    ContextT(CFGRewriteReducer *R, BasicBlock *C)
      : DefaultContext(R), Continuation(C)
    { }

    bool insideCFG() { return get()->currentBB_; }
    BasicBlock* continuation() { return Continuation; }

    ContextT getCurrentContinuation() {
      if (Continuation)
        return *this;
      else
        return ContextT(get(), get()->makeContinuation());
    }


    /// Pass the continuation only to SExprs in tail position.
    ContextT sub(TraversalKind K) const {
      if (K == TRV_Tail)
        return *this;
      else
        return ContextT(get(), nullptr);
    }

    /// Handle the result of a traversal.
    SExpr* handleResult(SExpr** E, SExpr* Result, TraversalKind K) {
      if (!insideCFG())     // no current block
        return Result;
      get()->addInstruction(Result);

      if (Continuation) {
        // If we have a continuation, then terminate current block,
        // and pass the result to the continuation.
        get()->createGoto(Continuation, Result);
        return nullptr;
      }
      return Result;
    }

    /// Handle traversals of more specialized types (e.g. BasicBlock, VarDecl)
    template<class T>
    T* handleResult(T** E, T* Result, TraversalKind K) { return Result; }

    /// Cast a result to the appropriate type.
    template<class T>
    T* castResult(T** E, SExpr* Result) { return cast<T>(Result); }


    void enterScope(VarDecl* Orig, VarDecl* Nvd) {
      return get()->enterScope(Orig, Nvd);
    }

    void exitScope(VarDecl* Orig) {
      return get()->exitScope(Orig);
    }

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


  /// Add BB to the current CFG, and start working on it.
  void startBlock(BasicBlock *BB);

  /// Terminate the current block with a branch instruction.
  /// This will create new blocks for the branches.
  Branch* createBranch(SExpr *Cond);

  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *Target, SExpr* Result);

  /// Creates a new CFG.
  /// Returns the exit block, for use as a continuation.
  BasicBlock* initCFG();

  /// Completes the CFG and returns it.
  SCFG* finishCFG();


protected:
  // Add new let variable to the current basic block.
  void addLetDecl(VarDecl* Nv);

  // Add a new instruction to the current basic block.
  void addInstruction(SExpr* E);

  // Create a new basic block.
  BasicBlock* addBlock();

  // Finish the current basic block, terminating it with Term.
  void finishBlock(Terminator* Term);

  // Make a new continuation
  BasicBlock* makeContinuation();

public:
  CFGRewriteReducer(MemRegionRef A)
    : CopyReducerBase(A), currentCFG_(nullptr), currentBB_(nullptr),
      currentInstrNum_(0), currentBlockNum_(2)
  { }

private:
  friend class ContextT;

  VarContext varCtx_;
  std::vector<SExpr*> instructionMap_;
  std::vector<SExpr*> blockMap_;

  SCFG*       currentCFG_;              // the current SCFG
  BasicBlock* currentBB_;               // the current basic block
  unsigned    currentInstrNum_;
  unsigned    currentBlockNum_;

  std::vector<SExpr*> currentArgs_;     // arguments in currentBB.
  std::vector<SExpr*> currentInstrs_;   // instructions in currentBB.
};



class CFGRewriter : public Traversal<CFGRewriter, CFGRewriteReducer> {
public:
  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *E, CtxT Ctx,
                            TraversalKind K = TRV_Normal);

  static SCFG* convertSExprToCFG(SExpr *E, MemRegionRef A);
};



}  // end namespace ohmu

#endif  // OHMU_CFG_REDUCER_H

