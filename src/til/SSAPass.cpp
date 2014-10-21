//===- SSAPass.cpp ---------------------------------------------*- C++ --*-===//
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


#include "SSAPass.h"

namespace ohmu {

using namespace clang::threadSafety::til;


SCFG* SSAPass::reduceSCFG_Begin(SCFG &Orig) {
  InplaceReducer::reduceSCFG_Begin(Orig);
  BInfoMap.resize(CurrentCFG->numBlocks());
  return &Orig;
}


SCFG* SSAPass::reduceSCFG_End(SCFG* Scfg) {
  replacePendingLoads();

  // TODO: clear the Future arena.
  Pending.clear();
  BInfoMap.clear();

  // Assign numbers to phi nodes.
  Scfg->renumber();

  return InplaceReducer::reduceSCFG_End(Scfg);
}


BasicBlock* SSAPass::reduceBasicBlockBegin(BasicBlock &Orig) {
  InplaceReducer::reduceBasicBlockBegin(Orig);

  // Warning -- adding blocks to BlockInfo will invalidate CurrentVarMap.
  CurrentVarMap = &BInfoMap[CurrentBB->blockID()].AllocVarMap;

  // Initialize variable map to the size of the dominator's map.
  // Local variables in the dominator are in scope.
  unsigned PSize = 0;
  if (CurrentBB->parent())
    PSize = BInfoMap[CurrentBB->parent()->blockID()].AllocVarMap.size();
  CurrentVarMap->resize(PSize, nullptr);

  return &Orig;
}


// This is the second pass of the SSA conversion, which looks up values for
// all loads, and replaces the loads.
void SSAPass::replacePendingLoads() {
  // Second pass:  Go back and replace all loads with phi nodes or values.
  CurrentBB = CurrentCFG->entry();
  unsigned CurrentInstrID = CurrentCFG->numInstructions();

  for (PendingFuture &PF : Pending) {
    auto *F = PF.Fut;
    SExpr *E = F->maybeGetResult();
    if (!E) {
      Load  *L = F->LoadInstr;
      Alloc *A = F->AllocInstr;
      BasicBlock *B = L->block();
      if (B != CurrentBB) {
        // We've switched to a new block.  Clear the cache.
        VarMapCache.clear();
        unsigned MSize = BInfoMap[B->blockID()].AllocVarMap.size();
        VarMapCache.resize(MSize, nullptr);
        CurrentBB = B;
      }
      E = lookupInPredecessors(L->block(), A->allocID());
      F->setResult(E);
    }
    assert(PF.IPos && "Unhandled Future.");

    Instruction* I2 = cast<Instruction>(E);
    if (PF.BBInstr) {
      // If I2 is a new, non-trivial instruction, then add it to the
      // current block.
      if (I2->instrID() == 0 && !I2->isTrivial() && !isa<Phi>(I2)) {
        *PF.IPos = I2;
        I2->setInstrID(CurrentInstrID++);
      }
      // Otherwise, eliminate it, because it's a reference to a previously
      // added instruction.
      else {
        *PF.IPos = nullptr;
      }
    }
    else {
      *PF.IPos = I2;
    }
  }
  VarMapCache.clear();
};



SExpr* SSAPass::reduceAlloc(Alloc &Orig, SExpr* E0) {
  if (CurrentBB) {
    // Add alloc to current var map.
    Orig.setAllocID(CurrentVarMap->size());
    CurrentVarMap->push_back(E0);
  }
  return &Orig;
}


SExpr* SSAPass::reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
  if (CurrentBB) {
    // Update current var map.
    if (auto* A = dyn_cast<Alloc>(E0)) {
      CurrentVarMap->at(A->allocID()) = E1;
      return E1;
    }
  }
  return &Orig;
}


SExpr* SSAPass::reduceLoad(Load &Orig, SExpr* E0) {
  if (CurrentBB) {
    // Replace load with value from current var map.
    if (auto* A = dyn_cast_or_null<Alloc>(E0)) {
      if (auto *Av = CurrentVarMap->at(A->allocID()))
        return Av;
      else
        return new (FutArena) FutureLoad(&Orig, A);
    }
  }
  return &Orig;
}



SExpr* SSAPass::lookupInCache(LocalVarMap *LvarMap, unsigned LvarID) {
  SExpr *E = LvarMap->at(LvarID);
  if (!E)
    return nullptr;

  // The cached value may be an incomplete and temporary Phi node,
  // that was later eliminated.
  // If so, grab the real value and update the cache.
  if (auto *Ph = dyn_cast<Phi>(E)) {
    if (Ph->status() == Phi::PH_SingleVal) {
      E = Ph->values()[0];
      LvarMap->at(LvarID) = E;
    }
  }
  return E;
}


Phi* SSAPass::makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds) {
  // Values don't match, so make a new phi node.
  auto *Ph = new (Arena) Phi(Arena, numPreds);
  // Fill it with the original value E
  for (unsigned j = 0; j < i; ++j)
    Ph->values().push_back(E);
  return Ph;
}


// Lookup value of local variable at the beginning of basic block B
SExpr* SSAPass::lookupInPredecessors(BasicBlock *B, unsigned LvarID) {
  if (B == CurrentBB) {
    // See if we have a cached value at the start of the current block.
    if (SExpr* E = VarMapCache[LvarID])
      return E;
  }

  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  SExpr* E  = nullptr;   //< The first value we find.
  SExpr* E2 = nullptr;   //< The second distinct value we find.
  Phi*   Ph = nullptr;   //< The Phi node we created (if any)
  bool Incomplete = false;                //< Is Ph incomplete?
  bool SetInBlock = LvarMap->at(LvarID);  //< Is var set within this block?
  unsigned i = 0;

  for (BasicBlock* P : B->predecessors()) {
    if (!Ph && !SetInBlock && P->blockID() >= B->blockID()) {
      // This is a back-edge, and we don't set the variable in this block.
      // Create a dummy Phi node to avoid infinite recursion before lookup.
      Ph = makeNewPhiNode(i, E, B->numPredecessors());
      Incomplete = true;
      LvarMap->at(LvarID) = Ph;
      SetInBlock = true;
    }

    E2 = lookup(P, LvarID);
    if (!SetInBlock) {
      // Lookup in P may force a lookup in the current block due to cycles.
      // If that happened, just return the previous answer.
      if (auto* CE = lookupInCache(LvarMap, LvarID))
        return CE;
    }
    if (!E)
      E = E2;

    if (Ph) {
      // We already have a phi node, so just copy E2 into it.
      Ph->values().push_back(E2);
      // If E2 is different, then mark the Phi node as complete.
      if (E2 != Ph && E2 != E)
        Incomplete = false;
    }
    else if (E2 != E) {
      // Values don't match, so we need a phi node.
      Ph = makeNewPhiNode(i, E, B->numPredecessors());
      Ph->values().push_back(E2);
      Incomplete = false;
    }
    ++i;
  }

  if (Ph) {
    if (Incomplete) {
      // Remove Ph from the LvarMap; LvarMap will be set to E in lookup()
      LvarMap->at(LvarID) = nullptr;
      // Ph may have been cached elsewhere, so mark it as single val.
      // It will be eliminated by lookupInCache/
      Ph->values()[0] = E;
      Ph->setStatus(Phi::PH_SingleVal);
    }
    else {
      // Valid Phi node; add it to the block and return it.
      E = Ph;
      B->addArgument(Ph);
    }
  }

  // Cache the result to avoid creating duplicate phi nodes.
  if (B == CurrentBB)
    VarMapCache[LvarID] = E;
  return E;
}


// Lookup value of local variable at the end of basic block B
SExpr* SSAPass::lookup(BasicBlock *B, unsigned LvarID) {
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  assert(LvarID < LvarMap->size());
  // Check to see if the variable was set in this block.
  if (auto* E = lookupInCache(LvarMap, LvarID))
    return E;
  // Lookup variable in predecessor blocks.
  auto* E = lookupInPredecessors(B, LvarID);
  // Cache the result.
  LvarMap->at(LvarID) = E;
  return E;
}


}  // end namespace ohmu

