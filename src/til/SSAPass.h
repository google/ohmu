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

#include "InplaceReducer.h"

namespace ohmu {
namespace til  {


// Map from local variables (allocID) to their definitions (SExpr*).
typedef std::vector<SExpr*> LocalVarMap;

// Maintain a variable map for each basic block.
struct SSABlockInfo {
  LocalVarMap AllocVarMap;
};


class SSAPass : public InplaceReducer<CopyAttr>,
                public AGTraversal<SSAPass> {
public:
  void enterCFG(SCFG *Cfg);
  void exitCFG(SCFG *Cfg);
  void enterBlock(BasicBlock *B);
  void exitBlock(BasicBlock *B);

  void traverseLoad (Load* Orig);
  void traverseStore(Store* Orig);

  void reduceWeak(Instruction* I);

  void reduceAlloc(Alloc *Orig);
  void reduceStore(Store *Orig);
  void reduceLoad (Load  *Orig);

protected:
  // An Alloc instruction that may be removed.
  class FutureAlloc : public Future {
  public:
    FutureAlloc(Alloc* A) : AllocInstr(A) { }
    virtual ~FutureAlloc() { }

    /// We don't use evaluate(); FutureAllocs are forced manually.
    virtual SExpr* evaluate() override { return nullptr; }

    Alloc *AllocInstr;
  };

  // A Store instruction that may be removed.
  class FutureStore : public Future {
  public:
    FutureStore(Store* S, Alloc* A) : StoreInstr(S), AllocInstr(A) { }
    virtual ~FutureStore() { }

    /// We don't use evaluate(); FutureStores are forced manually.
    virtual SExpr* evaluate() override { return nullptr; }

    Store *StoreInstr;
    Alloc *AllocInstr;
  };

  // A load instruction that needs to be rewritten.
  class FutureLoad : public Future {
  public:
    FutureLoad(Load* L, Alloc* A) : LoadInstr(L), AllocInstr(A) { }
    virtual ~FutureLoad() { }

    /// We don't use evaluate(); FutureLoads are forced manually.
    virtual SExpr* evaluate() override { return nullptr; }

    Load  *LoadInstr;
    Alloc *AllocInstr;
  };

  // Look up variable in the cache.
  SExpr* lookupInCache(LocalVarMap *LvarMap, unsigned LvarID);

  // Make a new phi node, with the first i values set to E
  Phi* makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds);

  // Lookup value of local variable at the beginning of basic block B
  SExpr* lookupInPredecessors(BasicBlock *B, unsigned LvarID, StringRef Nm);

  // Lookup value of local variable at the end of basic block B
  SExpr* lookup(BasicBlock *B, unsigned LvarID, StringRef Nm);

  // Second pass of SSA -- lookup variables and replace all loads.
  void replacePending();

public:
  SSAPass(MemRegionRef A)
      : InplaceReducer(A), CurrentVarMap(nullptr), CurrBB(nullptr) {
    FutArena.setRegion(&FutRegion);
  }

private:
  typedef InplaceReducer<CopyAttr> Super;
  typedef Traversal<SSAPass>       SuperTv;

  SSAPass() = delete;

  MemRegion    FutRegion;  ///< Put Futures in region for immediate deletion.
  MemRegionRef FutArena;

  LocalVarMap* CurrentVarMap;

  std::vector<SSABlockInfo> BInfoMap;      ///< Side table for basic blocks.
  std::vector<FutureLoad*>  PendingLoads;  ///< Loads that need to be forced.
  std::vector<FutureStore*> PendingStores; ///< Possibly removable stores.
  std::vector<FutureAlloc*> PendingAllocs; ///< Possibly removable allocs.
  LocalVarMap VarMapCache;

  BasicBlock* CurrBB;
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
