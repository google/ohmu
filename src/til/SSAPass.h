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

struct SSABlockInfo {
  LocalVarMap AllocVarMap;
};


class SSAPass : public InplaceReducer,
                public Traversal<SSAPass, SExprReducerMap>,
                public DefaultScopeHandler<SExprReducerMap> {
public:
  static void ssaTransform(SCFG* Scfg, MemRegionRef A) {
    SSAPass Pass(A);
    Pass.traverse(Scfg, TRV_Tail);
  }

  SCFG* reduceSCFG_Begin(SCFG &Orig);
  SCFG* reduceSCFG_End(SCFG* Scfg);

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig);

  template <class T>
  T* handleResult(SExpr** Eptr, T* Res) {
    if (Future *F = dyn_cast_or_null<Future>(Res))
      Pending.push_back(PendingFuture(Eptr, F));
    *Eptr = Res;
    return Res;
  }

  void handleFutureInstr(Instruction **Iptr, Future *F) override {
    Pending.push_back(PendingFuture(Iptr, F));
  }
  void handleFuturePhiArg(SExpr **Eptr, Future *F) override {
    Pending.push_back(PendingFuture(Eptr, F));
  }

  BasicBlock* handleResult(BasicBlock** B, BasicBlock* Res) { return Res; }
  VarDecl*    handleResult(VarDecl** VD,   VarDecl *Res)    { return Res; }
  Slot*       handleResult(Slot** S,       Slot *Res)       { return Res; }

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
    PendingFuture(Instruction **Pos, Future *F)
      : IPos(Pos), Fut(reinterpret_cast<FutureLoad*>(F)), BBInstr(true)
    { }

    PendingFuture(SExpr **Pos, Future *F)
      // The cast is safe here because IPos is write-only.
      : IPos(reinterpret_cast<Instruction**>(Pos)),
        Fut(reinterpret_cast<FutureLoad*>(F)), BBInstr(false)
    { }

    Instruction **IPos;   // The location where the future occurs.
    FutureLoad  *Fut;     // The future.
    bool        BBInstr;  // True if IPos is a basic block instruction.
  };

  // Look up variable in the cache.
  SExpr* lookupInCache(LocalVarMap *LvarMap, unsigned LvarID);

  // Make a new phi node, with the first i values set to E
  Phi* makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds);

  // Lookup value of local variable at the beginning of basic block B
  SExpr* lookupInPredecessors(BasicBlock *B, unsigned LvarID);

  // Lookup value of local variable at the end of basic block B
  SExpr* lookup(BasicBlock *B, unsigned LvarID);

  // Second pass of SSA -- lookup variables and replace all loads.
  void replacePendingLoads();

public:
  SSAPass(MemRegionRef A) : InplaceReducer(A), CurrentVarMap(nullptr) {
    FutArena.setRegion(&FutRegion);
  }

private:
  SSAPass() = delete;

  MemRegion    FutRegion; //< Allocate futures in region for immediate delete.
  MemRegionRef FutArena;

  LocalVarMap* CurrentVarMap;

  std::vector<SSABlockInfo>  BInfoMap;  //< Side table for basic blocks.
  std::vector<PendingFuture> Pending;   //< Loads that need to be rewritten.
  LocalVarMap VarMapCache;
};



}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
