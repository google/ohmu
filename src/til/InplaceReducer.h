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

#include "CFGBuilder.h"
#include "Scope.h"


namespace ohmu {
namespace til  {


/// InplaceReducer implements the reducer interface so that each reduce simply
/// returns a pointer to the original term.
///
/// It is intended to be used as a basic class for destructive in-place
/// transformations.
class InplaceReducer : public CFGBuilder, public ScopeHandler {
public:
  SExpr*   reduceWeak(Instruction* I) { return scope().lookupInstr(I); }
  VarDecl* reduceWeak(VarDecl *E)     { return E; }
  Slot*    reduceWeak(Slot *S)        { return S; }

  BasicBlock* reduceWeak(BasicBlock *B) {
    if (!Scope->lookupBlock(B))
      Scope->updateBlockMap(B, B);
    return B;
  }


  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    Orig.rewrite(E);
    return &Orig;
  }
  VarDecl* reduceVarDeclLetrec(VarDecl* Nvd, SExpr* D) { return Nvd; }

  SExpr* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    Orig.rewrite(Nvd, E0);
    return &Orig;
  }
  SExpr* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return &Orig;
  }
  SExpr* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return &Orig;
  }
  Slot* reduceSlot(Slot &Orig, SExpr *E0) {
    Orig.rewrite(E0);
    return &Orig;
  }
  Record* reduceRecordBegin(Record &Orig)    { return &Orig; }
  void handleRecordSlot(Record *E, Slot *Res) {
    /* Slots can only be replaced with themselves. */
  }
  Record* reduceRecordEnd(Record *R)         { return R;     }

  SExpr*  reduceScalarType(ScalarType &Orig) { return &Orig; }

  SExpr* reduceLiteral(Literal &Orig)        { return &Orig; }
  template<class T>
  SExpr* reduceLiteralT(LiteralT<T> &Orig)   { return &Orig; }

  SExpr* reduceVariable(Variable &Orig, VarDecl* Vd) {
    Orig.rewrite(Vd);
    return &Orig;
  }
  SExpr* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return &Orig;
  }
  SExpr* reduceProject(Project &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return &Orig;
  }

  SExpr* reduceCall(Call &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return addInstr(&Orig);
  }
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return addInstr(&Orig);
  }
  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return addInstr(&Orig);
  }
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return addInstr(&Orig);
  }
  SExpr* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return addInstr(&Orig);
  }
  SExpr* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return addInstr(&Orig);
  }
  SExpr* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return addInstr(&Orig);
  }
  SExpr* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    Orig.rewrite(E0, E1);
    return addInstr(&Orig);
  }
  SExpr* reduceCast(Cast &Orig, SExpr* E0) {
    Orig.rewrite(E0);
    return addInstr(&Orig);
  }

  /// Phi nodes require special handling, and cannot be
  /// Passes which reduce Phi nodes must also set OverwriteArguments to true.
  SExpr* reducePhi(Phi& Orig) { return &Orig; }

  Goto* reduceGotoBegin(Goto &Orig, BasicBlock *B) { return &Orig; }
  void handlePhiArg(Phi &Orig, Goto *NG, SExpr *Res) {
    rewritePhiArg(scope().lookupInstr(&Orig), NG, Res);
  }
  Goto* reduceGotoEnd(Goto* G) { return G; }

  SExpr* reduceBranch(Branch &Orig, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    Orig.rewrite(C, B0, B1);
    return &Orig;
  }
  SExpr* reduceReturn(Return &Orig, SExpr* E) {
    Orig.rewrite(E);
    return &Orig;
  }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    beginBlock(&Orig);
    return &Orig;
  }
  void handleBBArg(Phi &Orig, SExpr* Res) {
    if (OverwriteArguments)
      scope().updateInstructionMap(&Orig, Res);
  }
  void handleBBInstr(Instruction &Orig, SExpr* Res) {
    scope().updateInstructionMap(&Orig, Res);
  }
  BasicBlock* reduceBasicBlockEnd(BasicBlock *B, SExpr* Term) {
    endBlock(cast<Terminator>(Term));
    return B;
  }

  SCFG* reduceSCFG_Begin(SCFG &Orig);
  void handleCFGBlock(BasicBlock &Orig, BasicBlock* Res) {
    assert(&Orig == Res && "Blocks cannot be replaced.");
  }
  SCFG* reduceSCFG_End(SCFG* Scfg);

  SExpr* reduceUndefined (Undefined &Orig)  { return &Orig; }
  SExpr* reduceWildcard  (Wildcard &Orig)   { return &Orig; }
  SExpr* reduceIdentifier(Identifier &Orig) { return &Orig; }

  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    Orig.rewrite(Nvd, B);
    return &Orig;
  }
  SExpr* reduceLetrec(Letrec &Orig, VarDecl *Nvd, SExpr* B) {
    Orig.rewrite(Nvd, B);
    return &Orig;
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    Orig.rewrite(C, T, E);
    return &Orig;
  }

  InplaceReducer() {
    OverwriteInstructions = true;
  }
  InplaceReducer(MemRegionRef A) : CFGBuilder(A) {
    OverwriteInstructions = true;
  }
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_INPLACEREDUCER_H
