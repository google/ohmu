//===- CopyReducer.h -------------------------------------------*- C++ --*-===//
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
// CopyReducer implements the reducer interface to build a new SExpr;
// it makes a deep copy of a term.
//
// It is useful as a base class for more complex non-destructive rewrites.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_COPYREDUCER_H
#define OHMU_TIL_COPYREDUCER_H

#include "CFGBuilder.h"
#include "Scope.h"
#include "AttributeGrammar.h"

#include <cstddef>
#include <memory>
#include <queue>
#include <vector>


namespace ohmu {
namespace til  {


/// CopyReducer implements the reducer interface to build a new SExpr.
/// In other words, it makes a deep copy of a term.
/// It also useful as a base class for non-destructive rewrites.
/// It automatically performs variable substitution during the copy.

class CopyReducer : public CFGBuilder {
public:
  ScopeFrame& scope() { return *Scope; }

  /// Switch to a new scope, and return pointer to old scope.
  ScopeFrame* switchScope(ScopeFrame* NewScope) {
    auto* S = Scope; Scope = NewScope; return S;
  }

  /// Restore the previous scope.
  void restoreScope(ScopeFrame* OldScope) { Scope = OldScope; }


  void enterScope(VarDecl *Orig, VarDecl *Nvd) {
    assert(Scope && "Cannot rewrite in output scope.");
    // Rewrite old variable to the new variable.
    // reduceVariable does the actual replacement.
    Nvd->setVarIndex(Scope->allocVarIndex());
    Scope->enterScope(Orig, newVariable(Nvd));
  }
  void exitScope(VarDecl *Orig) {
    assert(Scope && "Cannot rewrite in output scope.");
    Scope->freeVarIndex();
    Scope->exitScope(Orig);
  }

  SExpr* reduceWeak(Instruction* E) {
    return Scope->lookupInstr(E);
  }
  VarDecl* reduceWeak(VarDecl *E) {
    // Ignored, since we override reduceVar.
    return nullptr;
  }
  inline BasicBlock* reduceWeak(BasicBlock *E);


  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return newVarDecl(Orig.kind(), Orig.varName(), E);
  }
  VarDecl* reduceVarDeclLetrec(VarDecl* VD, SExpr* E) {
    VD->setDefinition(E);
    return VD;
  }
  Function* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return newFunction(Nvd, E0);
  }
  Code* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    auto* Res = newCode(E0, E1);
    Res->setCallingConvention(Orig.callingConvention());
    return Res;
  }
  Field* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return newField(E0, E1);
  }
  Slot* reduceSlot(Slot &Orig, SExpr *E0) {
    auto* Res = newSlot(Orig.slotName(), E0);
    Res->setModifiers(Orig.modifiers());
    return Res;
  }
  Record* reduceRecordBegin(Record &Orig) {
    return newRecord(Orig.slots().size());
  }
  void handleRecordSlot(Record *R, Slot *S) {
    R->slots().emplace_back(Arena, S);
  }
  Record* reduceRecordEnd(Record *R) { return R; }

  SExpr*  reduceScalarType(ScalarType &Orig) {
    return &Orig;  // Scalar types are globally defined; we share pointers.
  }

  Literal* reduceLiteral(Literal &Orig) {
    return new (Arena) Literal(Orig);
  }
  template<class T>
  LiteralT<T>* reduceLiteralT(LiteralT<T> &Orig) {
    return newLiteralT<T>(Orig.value());
  }
  SExpr* reduceVariable(Variable &Orig, VarDecl* VD) {
    // Perform variable substitution
    return Scope->lookupVar(Orig.variableDecl());
  }
  Apply* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return newApply(E0, E1, Orig.applyKind());
  }
  Project* reduceProject(Project &Orig, SExpr* E0) {
    auto *Res = newProject(E0, Orig.slotName());
    Res->setArrow(Orig.isArrow());
    return Res;
  }

  Call* reduceCall(Call &Orig, SExpr* E0) {
    auto *Res = newCall(E0);
    Res->setCallingConvention(Res->callingConvention());
    return Res;
  }
  Alloc* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return newAlloc(E0, Orig.allocKind());
  }
  Load* reduceLoad(Load &Orig, SExpr* E0) {
    return newLoad(E0);
  }
  Store* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return newStore(E0, E1);
  }
  ArrayIndex* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return newArrayIndex(E0, E1);
  }
  ArrayAdd* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return newArrayAdd(E0, E1);
  }
  UnaryOp* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return newUnaryOp(Orig.unaryOpcode(), E0);
  }
  BinaryOp* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return newBinaryOp(Orig.binaryOpcode(), E0, E1);
  }
  Cast* reduceCast(Cast &Orig, SExpr* E0) {
    return newCast(Orig.castOpcode(), E0);
  }

  // Phi nodes are created and added to InstructionMap by reduceWeak(BB).
  // Passes which reduce Phi nodes must also set OverwriteArguments to true.
  SExpr* reducePhi(Phi& Orig) { return nullptr; }

  Goto* reduceGotoBegin(Goto &Orig, BasicBlock *B) {
    unsigned Idx = B->addPredecessor(CurrentBB);
    return new (Arena) Goto(B, Idx);
  }
  void handlePhiArg(Phi &Orig, Goto *NG, SExpr *Res) {
    rewritePhiArg(Scope->lookupInstr(&Orig), NG, Res);
  }
  Goto* reduceGotoEnd(Goto* G) {
    endBlock(G);      // Phi nodes are set by handlePhiNodeArg.
    return G;
  }

  Branch* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return newBranch(C, B0, B1);
  }
  Return* reduceReturn(Return &O, SExpr* E) {
    return newReturn(E);
  }

  inline BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig);
  void handleBBArg(Phi &Orig, SExpr* Res) {
    if (OverwriteArguments)
      scope().updateInstructionMap(&Orig, Res);
  }
  void handleBBInstr(Instruction &Orig, SExpr* Res) {
    scope().updateInstructionMap(&Orig, Res);
  }
  inline BasicBlock* reduceBasicBlockEnd(BasicBlock *B, SExpr* Term);

  inline SCFG* reduceSCFG_Begin(SCFG &Orig);
  void handleCFGBlock(BasicBlock &Orig, BasicBlock* Res) {
    /* BlockMap updated by reduceWeak(BasicBlock). */
  }
  inline SCFG* reduceSCFG_End(SCFG* Scfg);

  SExpr* reduceUndefined(Undefined &Orig) {
    return newUndefined();
  }
  SExpr* reduceWildcard(Wildcard &Orig) {
    return newWildcard();
  }

  SExpr* reduceIdentifier(Identifier &Orig) {
    return new (Arena) Identifier(Orig.idString());
  }
  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    return newLet(Nvd, B);
  }
  SExpr* reduceLetrec(Letrec &Orig, VarDecl *Nvd, SExpr* B) {
    return newLetrec(Nvd, B);
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return newIfThenElse(C, T, E);
  }

public:
  CopyReducer() : Scope(new ScopeFrame()) { }
  CopyReducer(MemRegionRef A) : CFGBuilder(A), Scope(new ScopeFrame()) { }
  ~CopyReducer() {
    if (Scope) delete Scope;
  }

public:
  // TODO: make private
  ScopeFrame* Scope;
};



/// An implementation of Future for lazy, non-destructive traversals.
/// Visitor extends CopyReducer.
template<class Visitor>
class LazyCopyFuture : public Future {
public:
  LazyCopyFuture(SExpr* E, Visitor* R, ScopeFrame* S)
    : PendingExpr(E), Reducer(R), Scope(S)
  { }
  ~LazyCopyFuture() {
    if (Scope) delete Scope;
  }

  /// Traverse PendingExpr and return the result.
  virtual SExpr* evaluate() override {
    auto* S = Reducer->switchScope(Scope);
    // TODO: store the context in which the future was created.
    auto* Res = Reducer->traverse(PendingExpr, TRV_Decl);
    Reducer->restoreScope(S);

    finish();
    return Res;
  }

protected:
  void finish() {
    delete Scope;
    Scope = 0;
    PendingExpr = nullptr;
  }

  SExpr*      PendingExpr;    // The expression to be rewritten
  Visitor*    Reducer;        // The reducer object.
  ScopeFrame* Scope;          // The context in which it occurs
};



/// Base class for non-destructive, lazy traversals.
template<class Self, class FutureType = LazyCopyFuture<Self> >
class LazyCopyTraversal : public Traversal<Self, SExprReducerMap> {
public:
  typedef Traversal<Self, SExprReducerMap> SuperTv;

  Self* self() { return static_cast<Self*>(this); }

  /// Factory method to create a future in the current context.
  /// Default implementation works for LazyFuture; override for other types.
  FutureType* makeFuture(SExpr *E) {
    auto *F = new (self()->arena())
      FutureType(E, self(), self()->Scope->clone());
    FutureQueue.push(F);
    return F;
  }

  /// Traverse E, returning a future if K == TRV_Lazy.
  MAPTYPE(SExprReducerMap, SExpr) traverse(SExpr *E, TraversalKind K) {
    if (K == TRV_Lazy || K == TRV_Type)
      return self()->makeFuture(E);
    return SuperTv::traverse(E, K);
  }

  /// Lazy traversals cannot be done on types other than SExpr.
  template<class T>
  MAPTYPE(SExprReducerMap, T) traverse(T *E, TraversalKind K) {
    return SuperTv::traverse(E, K);
  }

  /// Perform a lazy traversal.
  SExpr* traverseAll(SExpr *E) {
    SExpr *Result = self()->traverse(E, TRV_Tail);

    // Process futures in queue.
    while (!FutureQueue.empty()) {
      auto *F = FutureQueue.front();
      FutureQueue.pop();
      F->force();
    }
    return Result;
  }

protected:
  std::queue<FutureType*> FutureQueue;
};



/// This class will make a deep copy of a term.
class SExprCopier : public CopyReducer, public LazyCopyTraversal<SExprCopier> {
public:
  SExprCopier(MemRegionRef A) : CopyReducer(A) { }

  static SExpr* copy(SExpr *E, MemRegionRef A) {
    SExprCopier Copier(A);
    return Copier.traverseAll(E);
  }
};




SCFG* CopyReducer::reduceSCFG_Begin(SCFG &Orig) {
  beginCFG(nullptr, Orig.numBlocks(), Orig.numInstructions());
  Scope->enterCFG(&Orig, currentCFG());
  return currentCFG();
}

SCFG* CopyReducer::reduceSCFG_End(SCFG* Scfg) {
  Scope->exitCFG();
  endCFG();
  Scfg->renumber();
  return Scfg;
}


BasicBlock* CopyReducer::reduceBasicBlockBegin(BasicBlock &Orig) {
  BasicBlock *B = reduceWeak(&Orig);
  beginBlock(B);
  return B;
}


BasicBlock* CopyReducer::reduceBasicBlockEnd(BasicBlock *B, SExpr* Term) {
  // Sanity check.
  // If Term isn't null, then writing the terminator should end the block.
  if (currentBB())
    endBlock(nullptr);
  return B;
}


// Create new blocks on demand, as we encounter jumps to them.
BasicBlock* CopyReducer::reduceWeak(BasicBlock *B) {
  auto *B2 = Scope->lookupBlock(B);
  if (!B2) {
    // Create new block, and add all of its Phi nodes to InstructionMap.
    // This has to be done before we process a Goto.
    unsigned Nargs = B->arguments().size();
    B2 = newBlock(Nargs, B->numPredecessors());
    Scope->updateBlockMap(B, B2);
  }
  return B2;
}


}  // end namespace til
}  // end namespace ohmu


#endif  // OHMU_TIL_COPYREDUCER_H
