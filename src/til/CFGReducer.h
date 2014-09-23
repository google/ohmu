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

#include <cstddef>
#include <vector>

namespace ohmu {

using namespace clang::threadSafety::til;


class TILDebugPrinter : public PrettyPrinter<TILDebugPrinter, std::ostream> {
public:
  TILDebugPrinter() : PrettyPrinter(false, false, false) { }
};


/// Reducer class that builds a copy of an SExpr.
class CopyReducerBase : public SExprReducerMap,
                        public ReadReducer<SExprReducerMap> {
public:
  CopyReducerBase() {}
  CopyReducerBase(MemRegionRef A) : Arena(A) { }

  void setArena(MemRegionRef A) { Arena = A; }

public:
  Instruction* reduceWeak(Instruction* E)  { return nullptr; }
  VarDecl*     reduceWeak(VarDecl *E)      { return nullptr; }
  BasicBlock*  reduceWeak(BasicBlock *E)   { return nullptr; }

  SExpr* reduceLiteral(Literal &Orig) {
    return new (Arena) Literal(Orig);
  }
  template<class T>
  SExpr* reduceLiteralT(LiteralT<T> &Orig) {
    return new (Arena) LiteralT<T>(Orig);
  }
  SExpr* reduceLiteralPtr(LiteralPtr &Orig) {
    return new (Arena) LiteralPtr(Orig);
  }

  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return new (Arena) VarDecl(Orig, E);
  }
  SExpr* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) Function(Orig, Nvd, E0);
  }
  SExpr* reduceSFunction(SFunction &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) SFunction(Orig, Nvd, E0);
  }
  SExpr* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Code(Orig, E0, E1);
  }
  SExpr* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Field(Orig, E0, E1);
  }

  SExpr* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Apply(Orig, E0, E1);
  }
  SExpr* reduceSApply(SApply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) SApply(Orig, E0, E1);
  }
  SExpr* reduceProject(Project &Orig, SExpr* E0) {
    return new (Arena) Project(Orig, E0);
  }
  SExpr* reduceCall(Call &Orig, SExpr* E0) {
    return new (Arena) Call(Orig, E0);
  }
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return new (Arena) Alloc(Orig, E0);
  }
  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    return new (Arena) Load(Orig, E0);
  }
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Store(Orig, E0, E1);
  }
  SExpr* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) ArrayIndex(Orig, E0, E1);
  }
  SExpr* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) ArrayAdd(Orig, E0, E1);
  }
  SExpr* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return new (Arena) UnaryOp(Orig, E0);
  }
  SExpr* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) BinaryOp(Orig, E0, E1);
  }
  SExpr* reduceCast(Cast &Orig, SExpr* E0) {
    return new (Arena) Cast(Orig, E0);
  }

  Phi* reducePhiBegin(Phi &Orig) {
    return new (Arena) Phi(Orig, Arena);
  }
  void reducePhiArg(Phi &Orig, Phi* Ph, unsigned i, SExpr* E) {
    Ph->values().push_back(E);
  }
  Phi* reducePhi(Phi* Ph) { return Ph; }

  SExpr* reduceGoto(Goto &Orig, BasicBlock *B) {
    return new (Arena) Goto(Orig, B, 0);
  }
  SExpr* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return new (Arena) Branch(O, C, B0, B1);
  }
  SExpr* reduceReturn(Return &O, SExpr* E) {
    return new (Arena) Return(O, E);
  }


  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    return new (Arena) BasicBlock(Orig, Arena);
  }
  void reduceBasicBlockArg(BasicBlock *BB, unsigned i, SExpr* E) {
    if (Phi* Ph = dyn_cast<Phi>(E))
      BB->addArgument(Ph);
  }
  void reduceBasicBlockInstr(BasicBlock *BB, unsigned i, SExpr* E) {
    if (Instruction* I = dyn_cast<Instruction>(E))
      BB->addInstruction(I);
  }
  void reduceBasicBlockTerm (BasicBlock *BB, SExpr* E) {
    BB->setTerminator(dyn_cast<Terminator>(E));
  }
  BasicBlock* reduceBasicBlock(BasicBlock *BB) { return BB; }


  SCFG* reduceSCFGBegin(SCFG &Orig) {
    return new (Arena) SCFG(Orig, Arena);
  }
  void reduceSCFGBlock(SCFG* Scfg, unsigned i, BasicBlock* B) {
    Scfg->add(B);
  }
  SCFG* reduceSCFG(SCFG* Scfg) { return Scfg; }


  SExpr* reduceUndefined(Undefined &Orig) {
    return new (Arena) Undefined(Orig);
  }
  SExpr* reduceWildcard(Wildcard &Orig) {
    return new (Arena) Wildcard(Orig);
  }

  SExpr* reduceIdentifier(Identifier &Orig) {
    return new (Arena) Identifier(Orig);
  }
  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    return new (Arena) Let(Orig, Nvd, B);
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return new (Arena) IfThenElse(Orig, C, T, E);
  }

protected:
  MemRegionRef Arena;
};


template<class Self, class ReducerT>
class CopyTraversal : public Traversal<Self, ReducerT> {
public:
  static SExpr* rewrite(SExpr *E, MemRegionRef A) {
    Self Traverser;
    ReducerT Reducer;
    Reducer.setArena(A);
    return Traverser.traverse(E, &Reducer, TRV_Tail);
  }
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
    if (K != TRV_Tail)
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
    : CopyReducerBase(A), currentCFG_(nullptr), currentBB_(nullptr),
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

#endif  // OHMU_CFG_REDUCER_H

