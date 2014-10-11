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
  SSAPass(MemRegionRef A)
    : Arena(A), CurrentCFG(nullptr), CurrentBB(nullptr), CurrentBlockID(0),
      CurrentVarMap(nullptr), CurrentPL(nullptr)
  { }

  static void ssaTransform(SCFG* Scfg, MemRegionRef A) {
    SSAPass Pass(A);
    Pass.traverse(Scfg, TRV_Tail);
  }


  // Destructively update SExprs by writing results.
  template <class T>
  T* handleResult(SExpr** Eptr, T* Res) {
    *Eptr = Res;
    if (CurrentPL && static_cast<SExpr*>(Res) == CurrentPL->LoadInstr) {
      // NOTE: this could cause a type error if a Load does not rewrite to
      // an instruction.  We check in reduceSCFG.
      CurrentPL->DPos = Eptr;
      CurrentPL = nullptr;
    }
    return Res;
  }

  // There's no need to rewrite update anything that's not an SExpr**.
  template <class T, class U>
  T* handleResult(U** Eptr, T* Res) {
    return Res;
  }

  SCFG* reduceSCFGBegin(SCFG &Orig) {
    assert(CurrentCFG == nullptr && "Already in a CFG.");
    CurrentCFG = &Orig;
    BInfoMap.resize(CurrentCFG->numBlocks());
    return &Orig;
  }

  SCFG* reduceSCFG(SCFG* Scfg) {
    assert(Scfg == CurrentCFG && "Internal traversal error.");

    // Second pass:  Go back and replace all loads with phi nodes or values.
    for (PendingLoad &PL : Pending) {
      SExpr *E = lookupInPredecessors(PL.BB, PL.AllocID);
      assert(PL.DPos && "Unhandled Load");
      assert(isa<Instruction>(E) && "Loads must rewrite to instructions.");
      *PL.DPos = E;
    }

    Pending.clear();
    CurrentCFG = nullptr;
    BInfoMap.clear();

    // Assign numbers to phi nodes.
    Scfg->renumber();
    return Scfg;
  }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    assert(CurrentBB == nullptr && "Already in a basic block.");
    CurrentBB = &Orig;
    CurrentBlockID = CurrentBB->blockID();

    // warning -- make sure you don't add blocks to BlockInfo.
    CurrentVarMap = &BInfoMap[CurrentBlockID].AllocVarMap;

    // Initialize variable map to the size of the dominator's map.
    // Local variables in the dominator are in scope.
    unsigned PSize = 0;
    if (CurrentBB->parent())
      PSize = BInfoMap[CurrentBB->parent()->blockID()].AllocVarMap.size();
    CurrentVarMap->resize(PSize, nullptr);

    return &Orig;
  }

  BasicBlock* reduceBasicBlock(BasicBlock *BB) {
    assert(CurrentBB == BB && "Internal traversal error.");
    CurrentBB = nullptr;
    return BB;
  }


  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    if (CurrentBB) {
      // Add alloc to current var map.
      Orig.setAllocID(CurrentVarMap->size());
      CurrentVarMap->push_back(E0);
    }
    return &Orig;
  }

  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    if (CurrentBB) {
      // Update current var map.
      if (auto* A = dyn_cast<Alloc>(E0)) {
        CurrentVarMap->at(A->allocID()) = E1;
      }
    }
    return &Orig;
  }

  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    if (CurrentBB) {
      // Replace load with value from current var map.
      if (auto* A = dyn_cast<Alloc>(E0)) {
        if (auto *Av = CurrentVarMap->at(A->allocID()))
          return Av;
        else {
          assert(CurrentPL == nullptr && "Unhandled load.");
          Pending.push_back(PendingLoad(&Orig, CurrentBB, A->allocID()));
          // handleResult will pull this info out of CurrentPL when we return.
          CurrentPL = &Pending.back();
        }
      }
    }
    return &Orig;
  }

protected:
  struct PendingLoad {
    PendingLoad(Load *Ld, BasicBlock *B, unsigned ID)
      : LoadInstr(Ld), BB(B), AllocID(ID), DPos(nullptr)
    { }

    Load       *LoadInstr;
    BasicBlock *BB;
    unsigned   AllocID;
    SExpr      **DPos;
  };

  // Lookup value of local variable at the beginning of basic block B
  SExpr* lookupInPredecessors(BasicBlock *B, unsigned LvarID) {
    auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
    SExpr* E = nullptr;
    Phi* Ph = nullptr;
    unsigned i = 0;

    for (auto* P : B->predecessors()) {
      SExpr* E2 = lookup(P, LvarID);
      if (Ph) {
        // We already have a phi node, so just copy E2 into it.
        Ph->values().push_back(E2);
      }
      else if ((E && E2 != E) || (P->blockID() >= B->blockID())) {
        // Values don't match, so make a new phi node.
        Ph = new (Arena) Phi(Arena, B->numPredecessors());
        // Fill it with the original value E
        for (unsigned j = 0; j < i; ++j)
          Ph->values().push_back(E);
        // Add the new value.
        Ph->values().push_back(E2);

        if (!LvarMap->at(LvarID))
          LvarMap->at(LvarID) = Ph;
      }
      E = E2;
      ++i;
    }
    if (Ph) {
      E = Ph;
      B->addArgument(Ph);
    }
    // FIXME -- cache value at beginning of block.

    return E;
  }

  // Lookup value of local variable at the end of basic block B
  SExpr* lookup(BasicBlock *B, unsigned LvarID) {
    auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
    assert(LvarID < LvarMap->size());
    // Check to see if the variable was set in this block.
    if (auto* E = LvarMap->at(LvarID))
      return E;
    // Lookup variable in predecessor blocks, and store in the end map.
    auto* E = lookupInPredecessors(B, LvarID);
    LvarMap->at(LvarID) = E;
    return E;
  }

private:
  SSAPass() = delete;

  MemRegionRef Arena;
  SCFG*        CurrentCFG;
  BasicBlock*  CurrentBB;
  unsigned     CurrentBlockID;
  LocalVarMap* CurrentVarMap;
  PendingLoad* CurrentPL;

  std::vector<BlockInfo> BInfoMap;
  std::vector<PendingLoad> Pending;
};



}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
