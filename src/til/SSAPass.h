//===- SSAPass.h -----------------------------------------------*- C++ --*-===//
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
// Implements the conversion to SSA.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_SSAPASS_H
#define OHMU_TIL_SSAPASS_H


/// Reducer class that builds a copy of an SExpr.
class DestructiveReducerBase : public SExprReducerMap,
                               public WriteBaseReducer<SExprReducerMap> {
public:
  Instruction* reduceWeak(Instruction* E)  { return nullptr; }
  VarDecl*     reduceWeak(VarDecl *E)      { return nullptr; }
  BasicBlock*  reduceWeak(BasicBlock *E)   { return nullptr; }

  SExpr* reduceLiteral(Literal &Orig) {
    return Orig;
  }
  template<class T>
  SExpr* reduceLiteralT(LiteralT<T> &Orig) {
    return Orig;  
  }
  SExpr* reduceLiteralPtr(LiteralPtr &Orig) {
    return Orig;
  }

  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return Orig;
  }
  SExpr* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceSFunction(SFunction &Orig, VarDecl *Nvd, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }

  SExpr* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceSApply(SApply &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceProject(Project &Orig, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceCall(Call &Orig, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return Orig;
  }
  SExpr* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return Orig;
  }
  SExpr* reduceCast(Cast &Orig, SExpr* E0) {
    return Orig;
  }

  Phi* reducePhiBegin(Phi &Orig) {
    return Orig;
  }
  void reducePhiArg(Phi &Orig, Phi* Ph, unsigned i, SExpr* E) { }
  Phi* reducePhi(Phi* Ph) { return Ph; }

  SExpr* reduceGoto(Goto &Orig, BasicBlock *B) {
    return Orig;
  }
  SExpr* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return Orig;
  }
  SExpr* reduceReturn(Return &O, SExpr* E) {
    return Orig;
  }


  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    return Orig;
  }
  void reduceBasicBlockArg  (BasicBlock *BB, unsigned i, SExpr* E) { }
  void reduceBasicBlockInstr(BasicBlock *BB, unsigned i, SExpr* E) { }
  void reduceBasicBlockTerm (BasicBlock *BB, SExpr* E) { }
  BasicBlock* reduceBasicBlock(BasicBlock *BB) { return BB; }


  SCFG* reduceSCFGBegin(SCFG &Orig) {
    return Orig;
  }
  void reduceSCFGBlock(SCFG* Scfg, unsigned i, BasicBlock* B) { }
  SCFG* reduceSCFG(SCFG* Scfg) { return Scfg; }


  SExpr* reduceUndefined(Undefined &Orig) {
    return Orig;
  }
  SExpr* reduceWildcard(Wildcard &Orig) {
    return Orig;
  }

  SExpr* reduceIdentifier(Identifier &Orig) {
    return Orig;
  }
  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    return Orig;
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return Orig;
  }
};


struct BlockInfo {
  std::vector<SExpr*> AllocVarMap;
};


class SSAPass {
public:
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    Orig.setAllocPos(CurrentVarMap->size());
    return Orig;
  }

  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    if (Alloc* A = dyn_cast<Alloc>(E0)) { 
      if (auto *Av = (*CurrentVarMap)[A->allocPos()])
        return Av;   // Replace load with current value.
    }
    return Orig;
  }

private:
  std::vector<BlockInfo> BlockInfo
  std::vector<SExpr*>*   CurrentVarMap;
};


#endif  // OHMU_TIL_SSAPASS_H
