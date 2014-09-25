//===- InplaceReducer.h ----------------------------------------*- C++ --*-===//
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
// InplaceReducer implements the reducer interface so that each reduce simply
// returns a pointer to the original term.
//
// It is intended to be used as a basic class for destructive in-place
// transformations.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_INPLACEREDUCER_H
#define OHMU_TIL_INPLACEREDUCER_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

namespace ohmu {

using namespace clang::threadSafety::til;


/// InplaceReducer implements the reducer interface so that each reduce simply
/// returns a pointer to the original term.
///
/// It is intended to be used as a basic class for destructive in-place
/// transformations.
class InplaceReducer : public SExprReducerMap,
                       public WriteBackReducer<SExprReducerMap> {
public:
  Instruction* reduceWeak(Instruction* E)  { return E; }
  VarDecl*     reduceWeak(VarDecl *E)      { return E; }
  BasicBlock*  reduceWeak(BasicBlock *E)   { return E; }

  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return &Orig;
  }
  SExpr* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceSFunction(SFunction &Orig, VarDecl *Nvd, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }

  SExpr* reduceLiteral(Literal &Orig) {
    return &Orig;
  }
  template<class T>
  SExpr* reduceLiteralT(LiteralT<T> &Orig) {
    return &Orig;
  }
  SExpr* reduceLiteralPtr(LiteralPtr &Orig) {
    return &Orig;
  }
  SExpr* reduceVariable(Variable &Orig) {
    return &Orig;
  }

  SExpr* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceSApply(SApply &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceProject(Project &Orig, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceCall(Call &Orig, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return &Orig;
  }
  SExpr* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return &Orig;
  }
  SExpr* reduceCast(Cast &Orig, SExpr* E0) {
    return &Orig;
  }

  Phi* reducePhiBegin(Phi &Orig) {
    return &Orig;
  }
  void reducePhiArg(Phi &Orig, Phi* Ph, unsigned i, SExpr* E) { }
  Phi* reducePhi(Phi* Ph) { return Ph; }

  SExpr* reduceGoto(Goto &Orig, BasicBlock *B) {
    return &Orig;
  }
  SExpr* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return &O;
  }
  SExpr* reduceReturn(Return &Orig, SExpr* E) {
    return &Orig;
  }


  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    return &Orig;
  }
  void reduceBasicBlockArg  (BasicBlock *BB, unsigned i, SExpr* E) { }
  void reduceBasicBlockInstr(BasicBlock *BB, unsigned i, SExpr* E) { }
  void reduceBasicBlockTerm (BasicBlock *BB, SExpr* E) { }
  BasicBlock* reduceBasicBlock(BasicBlock *BB) { return BB; }


  SCFG* reduceSCFGBegin(SCFG &Orig) {
    return &Orig;
  }
  void reduceSCFGBlock(SCFG* Scfg, unsigned i, BasicBlock* B) { }
  SCFG* reduceSCFG(SCFG* Scfg) { return Scfg; }


  SExpr* reduceUndefined(Undefined &Orig) {
    return &Orig;
  }
  SExpr* reduceWildcard(Wildcard &Orig) {
    return &Orig;
  }

  SExpr* reduceIdentifier(Identifier &Orig) {
    return &Orig;
  }
  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    return &Orig;
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return &Orig;
  }
};


}  // end namespace ohmu

#endif  // OHMU_TIL_INPLACEREDUCER_H
