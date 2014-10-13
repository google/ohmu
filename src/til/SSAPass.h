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

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

#include "til/InplaceReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;

// Map from local variables (allocID) to their definitions (SExpr*).
typedef std::vector<SExpr*> LocalVarMap;

struct BlockInfo {
  LocalVarMap AllocVarMap;
};


class SSAPass : public InplaceReducer,
                public Traversal<SSAPass, InplaceReducerMap>,
                public DefaultScopeHandler<InplaceReducerMap> {
public:
  static void ssaTransform(SCFG* Scfg, MemRegionRef A) {
    SSAPass Pass(A);
    Pass.traverse(Scfg, TRV_Tail);
  }


  SCFG* reduceSCFGBegin(SCFG &Orig);
  SCFG* reduceSCFG(SCFG* Scfg);

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig);
  BasicBlock* reduceBasicBlock(BasicBlock *BB);

  void reduceBBArgument(BasicBlock *BB, unsigned i, SExpr* E);
  void reduceBBInstruction(BasicBlock *BB, unsigned i, SExpr* E);

  // Destructively update SExprs by writing results.
  template <class T>
  T* handleResult(SExpr** Eptr, T* Res) {
    if (Future *F = dyn_cast_or_null<Future>(Res))
      Pending.push_back(PendingFuture(F, Eptr));
    *Eptr = Res;
    return Res;
  }

  BasicBlock* handleResult(BasicBlock** B, BasicBlock* Res) { return Res; }
  VarDecl*    handleResult(VarDecl** VD,   VarDecl *Res)    { return Res; }


  Instruction* reduceWeak(Instruction* I) {
    // In most cases, we can return the rewritten version of I.
    // However, phi node arguments on back-edges have not been rewritten yet;
    // we'll rewrite those when we do the jump.
    if (I->instrID() <= CurrentInstrID)
      return InstructionMap[I->instrID()];
    else
      return I;
  }

  BasicBlock* reduceWeak(BasicBlock* B) { return B; }
  VarDecl*    reduceWeak(VarDecl* VD)   { return VD; }

  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0);
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1);
  SExpr* reduceLoad(Load &Orig, SExpr* E0);


protected:
  // A load instruction that needs to be rewritten.
  class FutureLoad : public Future {
  public:
    FutureLoad(Load* L, Alloc* A) : LoadInstr(L), AllocInstr(A) { }
    ~FutureLoad() LLVM_DELETED_FUNCTION;

    Load  *LoadInstr;
    Alloc *AllocInstr;
  };

  // Pointer to a Future object, along with the position where it occurs.
  // Forcing the future will write the result to the position.
  struct PendingFuture {
    PendingFuture(Future *F, Instruction **Pos, unsigned N)
      : Fut(reinterpret_cast<FutureLoad*>(F)), IPos(Pos), INum(N) { }

    // The cast is safe here because IPos is write-only.
    PendingFuture(Future *F, SExpr **Pos)
      : Fut(reinterpret_cast<FutureLoad*>(F)),
        IPos(reinterpret_cast<Instruction**>(Pos)), INum(0) { }

    FutureLoad  *Fut;
    Instruction **IPos;
    unsigned INum;
  };

  // Make a new phi node, with the first i values set to E
  Phi* makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds);

  // Lookup value of local variable at the beginning of basic block B
  SExpr* lookupInPredecessors(BasicBlock *B, unsigned LvarID);

  // Lookup value of local variable at the end of basic block B
  SExpr* lookup(BasicBlock *B, unsigned LvarID);

public:
  SSAPass(MemRegionRef A)
    : Arena(A), CurrentCFG(nullptr), CurrentBB(nullptr), CurrentInstrID(0),
      CurrentVarMap(nullptr)
  { FutArena.setRegion(&FutRegion); }

private:
  SSAPass() = delete;

  MemRegionRef Arena;
  MemRegion    FutRegion; //< Allocate futures in region for immediate delete.
  MemRegionRef FutArena;

  SCFG*        CurrentCFG;
  BasicBlock*  CurrentBB;
  unsigned     CurrentInstrID;
  LocalVarMap* CurrentVarMap;

  std::vector<BlockInfo>     BInfoMap;
  std::vector<Instruction*>  InstructionMap;
  std::vector<PendingFuture> Pending;
};



}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
