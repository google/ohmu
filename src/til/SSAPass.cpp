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
namespace til  {

void SSAPass::enterCFG(SCFG *Cfg) {
  InplaceReducer::enterCFG(Cfg);
  BInfoMap.resize(Builder.currentCFG()->numBlocks());
}


void SSAPass::exitCFG(SCFG *Cfg) {
  replacePendingLoads();
  CurrBB = nullptr;

  // TODO: clear the Future arena.
  Pending.clear();
  BInfoMap.clear();

  InplaceReducer::exitCFG(Cfg);
}


void SSAPass::enterBlock(BasicBlock *B) {
  InplaceReducer::enterBlock(B);

  auto* Cbb = Builder.currentBB();

  // Warning -- adding blocks to BlockInfo will invalidate CurrentVarMap.
  CurrentVarMap = &BInfoMap[Cbb->blockID()].AllocVarMap;

  // Initialize variable map to the size of the dominator's map.
  // Local variables in the dominator are in scope.
  unsigned PSize = 0;
  if (Cbb->parent())
    PSize = BInfoMap[Cbb->parent()->blockID()].AllocVarMap.size();
  CurrentVarMap->resize(PSize, nullptr);
}


void SSAPass::exitBlock(BasicBlock *B) {

}


void SSAPass::reduceAlloc(Alloc *Orig) {
  assert(Orig->instrID() > 0 && "Alloc must be a top-level instruction.");
  auto* E0 = attr(0).Exp;

  if (Builder.currentBB()) {
    auto* Fld = dyn_cast<Field>(E0);
    if (Fld) {
      // Add alloc to current var map.
      Orig->setAllocID(CurrentVarMap->size());
      CurrentVarMap->push_back(Fld->body());
      resultAttr().Exp = nullptr;   // Remove Alloc instruction
      return;
    }
  }
  Super::reduceAlloc(Orig);
}


void SSAPass::reduceStore(Store *Orig) {
  // Alloc is rewritten to nullptr above, so attr(0).Exp == nullptr
  auto* E0 = Orig->destination();
  auto* E1 = attr(1).Exp;

  if (Builder.currentBB()) {
    auto* A = dyn_cast<Alloc>(E0);
    if (A) {
      // Update current var map.
      CurrentVarMap->at(A->allocID()) = E1;
      resultAttr().Exp = nullptr;   // Remove Store instruction
      return;
    }
  }
  Super::reduceStore(Orig);
}


void SSAPass::reduceLoad(Load *Orig) {
  // Alloc is rewritten to nullptr above, so attr(0).Exp == nullptr
  auto* E0 = Orig->pointer();

  if (Builder.currentBB()) {
    auto* A = dyn_cast_or_null<Alloc>(E0);
    if (A) {
      if (auto *Av = CurrentVarMap->at(A->allocID())) {
        // Replace load with value from current var map.
        resultAttr().Exp = Av;
        return;
      }
      else {
        // Replace load with future
        auto *F = new (FutArena) FutureLoad(A);
        Pending.push_back(F);
        resultAttr().Exp = F;
        return;
      }
    }
  }
  Super::reduceLoad(Orig);
}



// This is the second pass of the SSA conversion, which looks up values for
// all loads, and replaces the loads.
void SSAPass::replacePendingLoads() {
  // Second pass:  Go back and replace all loads with phi nodes or values.
  // VarMapCache holds lookups that we've already done in the current block.
  CurrBB = Builder.currentCFG()->entry();
  unsigned MSize = BInfoMap[CurrBB->blockID()].AllocVarMap.size();
  VarMapCache.resize(MSize, nullptr);

  for (auto *F : Pending) {
    SExpr *E = F->maybeGetResult();
    if (!E) {
      Alloc *A = F->AllocInstr;
      BasicBlock *B = F->block();
      if (B != CurrBB) {
        // We've switched to a new block.  Clear the cache.
        VarMapCache.clear();
        MSize = BInfoMap[B->blockID()].AllocVarMap.size();
        VarMapCache.resize(MSize, nullptr);
        CurrBB = B;
      }
      E = lookupInPredecessors(B, A->allocID(), A->instrName());
      F->setResult(E);
    }
  }
  VarMapCache.clear();
}


SExpr* SSAPass::lookupInCache(LocalVarMap *LvarMap, unsigned LvarID) {
  SExpr *E = LvarMap->at(LvarID);
  if (!E)
    return nullptr;

  // The cached value may be an incomplete and temporary Phi node, that was
  // later eliminated. If so, grab the real value and update the cache.
  if (auto *Ph = dyn_cast<Phi>(E)) {
    if (Ph->status() == Phi::PH_SingleVal) {
      E = Ph->values()[0].get();
      LvarMap->at(LvarID) = E;
    }
  }
  return E;
}


Phi* SSAPass::makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds) {
  // Values don't match, so make a new phi node.
  auto *Ph = new (arena()) Phi(arena(), numPreds);
  // Fill it with the original value E
  for (unsigned j = 0; j < i; ++j)
    Ph->values().emplace_back(arena(), E);
  if (Instruction *I = dyn_cast<Instruction>(E))
    Ph->setBaseType(I->baseType());
  return Ph;
}


// Lookup value of local variable at the beginning of basic block B
SExpr* SSAPass::lookupInPredecessors(BasicBlock *B, unsigned LvarID,
                                     StringRef Nm) {
  if (B == CurrBB) {
    // See if we have a cached value at the start of the current block.
    if (SExpr* E = VarMapCache[LvarID])
      return E;
  }

  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  SExpr* E  = nullptr;   // The first value we find.
  SExpr* E2 = nullptr;   // The second distinct value we find.
  Phi*   Ph = nullptr;   // The Phi node we created (if any)
  bool Incomplete = false;                // Is Ph incomplete?
  bool SetInBlock = LvarMap->at(LvarID);  // Is var set within this block?
  unsigned i = 0;

  for (auto &P : B->predecessors()) {
    if (!Ph && !SetInBlock && P->blockID() >= B->blockID()) {
      // This is a back-edge, and we don't set the variable in this block.
      // Create a dummy Phi node to avoid infinite recursion before lookup.
      Ph = makeNewPhiNode(i, E, B->numPredecessors());
      Incomplete = true;
      LvarMap->at(LvarID) = Ph;
      SetInBlock = true;
    }

    E2 = lookup(P.get(), LvarID, Nm);
    if (!SetInBlock) {
      // Lookup in P may have forced a lookup in the current block due to
      // cycles.  Check the cache to see if that happened, and if it did,
      /// just return the cached answer.
      if (auto* CE = lookupInCache(LvarMap, LvarID))
        return CE;
    }
    if (!E)
      E = E2;

    if (Ph) {
      // We already have a phi node, so just copy E2 into it.
      Ph->values().emplace_back(arena(), E2);
      // If E2 is different, then mark the Phi node as complete.
      if (E2 != Ph && E2 != E)
        Incomplete = false;
    }
    else if (E2 != E) {
      // Values don't match, so we need a phi node.
      Ph = makeNewPhiNode(i, E, B->numPredecessors());
      Ph->values().emplace_back(arena(), E2);
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
      Ph->values()[0].reset(E);
      Ph->setStatus(Phi::PH_SingleVal);
    }
    else {
      // Valid Phi node; add it to the block and return it.
      E = Ph;
      Ph->setInstrName(Builder, Nm);
      B->addArgument(Ph);
    }
  }

  // Cache the result to avoid creating duplicate phi nodes.
  if (B == CurrBB) {
    VarMapCache[LvarID] = E;
  }
  return E;
}


// Lookup value of local variable at the end of basic block B
SExpr* SSAPass::lookup(BasicBlock *B, unsigned LvarID, StringRef Nm) {
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  assert(LvarID < LvarMap->size());
  // Check to see if the variable was set in this block.
  if (auto* E = lookupInCache(LvarMap, LvarID))
    return E;
  // Lookup variable in predecessor blocks.
  auto* E = lookupInPredecessors(B, LvarID, Nm);
  // Cache the result.
  LvarMap->at(LvarID) = E;
  return E;
}


}  // end namespace til
}  // end namespace ohmu

