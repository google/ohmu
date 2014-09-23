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
#include <vector>

namespace ohmu {

using namespace clang::threadSafety::til;


class TILDebugPrinter : public PrettyPrinter<TILDebugPrinter, std::ostream> {
public:
  TILDebugPrinter() : PrettyPrinter(false, false, false) { }
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

  bool enterSubExpr(SExpr *E, TraversalKind K) {
    if (K == TRV_Tail)
      pushContinuation(currentContinuation());
    else
      pushContinuation(nullptr);
    return true;
  }

  template <class T>
  T* exitSubExpr(SExpr *E, T* Res, TraversalKind K) {
    if (!currentBB_)
      return Res;

    addInstruction(Res);

    // If we have a continuation, then jump to it.
    BasicBlock *b = popContinuation();
    if (b) {
      assert(K == TRV_Tail);
      createGoto(b, Res);
      return nullptr;
    }
    return Res;
  }

  std::nullptr_t skipTraverse(SExpr *E) { return nullptr; }


  void enterScope(VarDecl *Orig, VarDecl *Nv);
  void exitScope(const VarDecl *Orig);

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
  void startBlock(BasicBlock *BB, BasicBlock *Cont);

  /// Terminate the current block with a branch instruction.
  /// This will create new blocks for the branches.
  Branch* createBranch(SExpr *Cond);

  /// Terminate the current block with a Goto instruction.
  Goto* createGoto(BasicBlock *Target, SExpr* Result);

  /// Creates a new CFG.
  /// Returns the exit block, for use as a continuation.
  void initCFG();

  /// Completes the CFG and returns it.
  SCFG* finishCFG();


protected:
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
    : CopyReducer(A), currentCFG_(nullptr), currentBB_(nullptr),
      currentInstrNum_(0), currentBlockNum_(2)
  { }

private:
  friend class ContextT;
  friend class CFGRewriter;

  VarContext varCtx_;
  std::vector<SExpr*> instructionMap_;
  std::vector<SExpr*> blockMap_;

  SCFG*       currentCFG_;              //< the current SCFG
  BasicBlock* currentBB_;               //< the current basic block
  unsigned    currentInstrNum_;
  unsigned    currentBlockNum_;

  std::vector<Phi*>         currentArgs_;        //< arguments in currentBB.
  std::vector<Instruction*> currentInstrs_;      //< instructions in currentBB.
  std::vector<BasicBlock*>  continuationStack_;
};



class CFGRewriter : public Traversal<CFGRewriter, CFGRewriteReducer> {
public:
  // IfThenElse requires a special traverse, because it involves creating
  // additional basic blocks.
  SExpr* traverseIfThenElse(IfThenElse *E, CFGRewriteReducer *R,
                            TraversalKind K);

  static SCFG* convertSExprToCFG(SExpr *E, MemRegionRef A);
};



}  // end namespace ohmu

#endif  // OHMU_TIL_CFGREDUCER_H

