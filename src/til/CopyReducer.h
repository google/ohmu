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


#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

#include "til/CFGBuilder.h"
#include "til/VarContext.h"


namespace ohmu {

using namespace clang::threadSafety::til;


/// CopyReducer implements the reducer interface to build a new SExpr.
/// In other words, it makes a deep copy of a term.
/// It is also useful as a base class for non-destructive rewrites.
class CopyReducer : public CFGBuilder, public ScopeHandler {
public:
  SExpr* reduceWeak(Instruction* E) {
    return InstructionMap[E->instrID()];
  }

  VarDecl* reduceWeak(VarDecl *E) {
    return VarCtx->map(E->varIndex());
  }

  BasicBlock* reduceWeak(BasicBlock *E);

  // This is a non-destructive rewrite; just return the result.
  template <class T, class U>
  T* handleResult(T** Eptr, U* Res) { return Res; }

  void handleRecordSlot(Record *E, Slot *Res) {
    E->slots().push_back(Res);
  }
  void handlePhiArg(Phi &Orig, Goto *NG, SExpr *Res) {
    rewritePhiArg(Orig, NG, Res);
  }
  void handleBBArg(Phi &Orig, SExpr* Res) {
    if (OverwriteArguments && Orig.instrID() > 0)
      InstructionMap[Orig.instrID()] = Res;
  }
  void handleBBInstr(Instruction &Orig, SExpr* Res) {
    if (Orig.instrID() > 0)
      InstructionMap[Orig.instrID()] = Res;
  }
  void handleCFGBlock(BasicBlock &Orig, BasicBlock* Res) {
    /* BlockMap updated by reduceWeak(BasicBlock). */
  }

  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return new (Arena) VarDecl(Orig, E);
  }
  VarDecl* reduceVarDeclLetrec(VarDecl* VD, SExpr* E) {
    VD->setDefinition(E);
    return VD;
  }
  Function* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) Function(Orig, Nvd, E0);
  }
  SFunction* reduceSFunction(SFunction &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) SFunction(Orig, Nvd, E0);
  }
  Code* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Code(Orig, E0, E1);
  }
  Field* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Field(Orig, E0, E1);
  }
  Slot* reduceSlot(Slot &Orig, SExpr *E0) {
    return new (Arena) Slot(Orig, E0);
  }
  Record* reduceRecordBegin(Record &Orig) {
    return new (Arena) Record(Orig, Arena);
  }
  Record* reduceRecordEnd(Record *R) { return R; }


  Literal* reduceLiteral(Literal &Orig) {
    return new (Arena) Literal(Orig);
  }
  template<class T>
  LiteralT<T>* reduceLiteralT(LiteralT<T> &Orig) {
    return new (Arena) LiteralT<T>(Orig);
  }
  LiteralPtr* reduceLiteralPtr(LiteralPtr &Orig) {
    return new (Arena) LiteralPtr(Orig);
  }
  Variable* reduceVariable(Variable &Orig, VarDecl* VD) {
    return new (Arena) Variable(Orig, VD);
  }

  Apply* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Apply(Orig, E0, E1);
  }
  SApply* reduceSApply(SApply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) SApply(Orig, E0, E1);
  }
  Project* reduceProject(Project &Orig, SExpr* E0) {
    return new (Arena) Project(Orig, E0);
  }

  Call* reduceCall(Call &Orig, SExpr* E0) {
    return addInstr(new (Arena) Call(Orig, E0));
  }
  Alloc* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return addInstr(new (Arena) Alloc(Orig, E0));
  }
  Load* reduceLoad(Load &Orig, SExpr* E0) {
    return addInstr(new (Arena) Load(Orig, E0));
  }
  Store* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) Store(Orig, E0, E1));
  }
  ArrayIndex* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) ArrayIndex(Orig, E0, E1));
  }
  ArrayAdd* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) ArrayAdd(Orig, E0, E1));
  }
  UnaryOp* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return addInstr(new (Arena) UnaryOp(Orig, E0));
  }
  BinaryOp* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) BinaryOp(Orig, E0, E1));
  }
  Cast* reduceCast(Cast &Orig, SExpr* E0) {
    return addInstr(new (Arena) Cast(Orig, E0));
  }

  /// Phi nodes are created and added to InstructionMap by reduceWeak(BB).
  /// Passes which reduce Phi nodes must also set OverwriteArguments to true.
  SExpr* reducePhi(Phi& Orig) { return nullptr; }

  Goto* reduceGotoBegin(Goto &Orig, BasicBlock *B) {
    unsigned Idx = B->addPredecessor(CurrentBB);
    return new (Arena) Goto(Orig, B, Idx);
  }
  Goto* reduceGotoEnd(Goto* G) {
    // Phi nodes are set by handlePhiNodeArg.
    endBlock(G);
    return G;
  }

  Branch* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return newBranch(C, B0, B1);
  }
  Return* reduceReturn(Return &O, SExpr* E) {
    auto *Rt = new (Arena) Return(O, E);
    endBlock(Rt);
    return Rt;
  }

  SCFG* reduceSCFG_Begin(SCFG &Orig);
  SCFG* reduceSCFG_End(SCFG* Scfg);

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig);
  BasicBlock* reduceBasicBlockEnd(BasicBlock *B, SExpr* Term);

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
  SExpr* reduceLetrec(Letrec &Orig, VarDecl *Nvd, SExpr* B) {
    return new (Arena) Letrec(Orig, Nvd, B);
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return new (Arena) IfThenElse(Orig, C, T, E);
  }

public:
  CopyReducer() { }
  CopyReducer(MemRegionRef A) : CFGBuilder(A) { }
};


/*
template<class Self>
class CopyTraversal : public Traversal<Self, SExprReducerMap> {

public:

};
*/


/// This class will make a deep copy of a term.
class SExprCopier : public CopyReducer,
                    public Traversal<SExprCopier, SExprReducerMap> {
public:
  SExprCopier(MemRegionRef a) : CopyReducer(a) { }

  static SExpr* copy(SExpr* e, MemRegionRef a) {
    SExprCopier copier(a);
    return copier.traverse(e, TRV_Tail);
  }
};


}  // end namespace ohmu


#endif  // OHMU_TIL_COPYREDUCER_H
