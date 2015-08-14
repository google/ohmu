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
  NumUses.resize(Cfg->numInstructions(), 0);
}


void SSAPass::exitCFG(SCFG *Cfg) {
  replacePending();

  // TODO: clear the Future arena.
  BInfoMap.clear();
  NumUses.clear();

  InplaceReducer::exitCFG(Cfg);
}


void SSAPass::enterBlock(BasicBlock *B) {
  InplaceReducer::enterBlock(B);

  auto* Cbb = Builder.currentBB();

  // Warning -- adding blocks to BlockInfo will invalidate CurrentVarMap.
  CurrentVarMap = &BInfoMap[Cbb->blockID()].AllocVarMap;

  // Initialize variable map to the size of the dominator's map.
  // Local variables in the dominator are in scope.
  unsigned PSize;
  if (Cbb->parent())
    PSize = BInfoMap[Cbb->parent()->blockID()].AllocVarMap.size();
  else
    PSize = 1;   // ID of zero means invalid ID.

  CurrentVarMap->resize(PSize, nullptr);
}


void SSAPass::exitBlock(BasicBlock *B) {
  InplaceReducer::exitBlock(B);
}


void SSAPass::reduceWeak(Instruction *I) {
  // Find all uses of alloc instructions to see if they can be eliminated.
  // We don't count loads and stores, and so decrement the counter there.
  if (I->instrID() > 0 && isa<Alloc>(I)) {
    ++NumUses[I->instrID()];
  }
  Super::reduceWeak(I);
}


void SSAPass::reduceAlloc(Alloc *Orig) {
  assert(Orig->instrID() > 0 && "Alloc must be a top-level instruction.");

  // Rewrite the alloc, in case we need it.
  Super::reduceAlloc(Orig);
  auto* E0 = attr(0).Exp;

  Orig->setAllocID(0);   // Invalidate ID, just to be sure.

  if (Builder.currentBB() && !Orig->isHeap()) {
    auto* Fld = dyn_cast<Field>(E0);
    SExpr* Fbdy = Fld ? Fld->body() : nullptr;
    if (Fld && (Fbdy == nullptr || isa<Instruction>(Fbdy))) {
      // Add the new variable to the map for the current block.
      unsigned Id = CurrentVarMap->size();
      Orig->setAllocID(Id);

      if (Fbdy) {
        CurrentVarMap->push_back(Fbdy);
      }
      else {
        // Variable is undefined or has an invalid definition.
        // Push undefined value into variable map.
        // It will hopefully be defined before a load instruction;
        // otherwise we can't eliminate it.
        auto* Un = Builder.newUndefined();
        if (auto* Ty = dyn_cast<ScalarType>(Fld->range())) {
          Un->setBaseType(Ty->baseType());
        }
        CurrentVarMap->push_back(Un);
      }

      // Reset uses to zero.
      NumUses[Orig->instrID()] = 0;

      // Return future, which will delete the Alloc later if not needed.
      auto *F = new (FutArena) FutureAlloc(Orig);
      PendingAllocs.push_back(F);
      resultAttr().Exp = F;
    }
  }
}


void SSAPass::reduceStore(Store *Orig) {
  // Alloc is rewritten to a future, so grab the original
  auto* E0 = Orig->destination();

  // Rewrite the store, in case we need it.
  Super::reduceStore(Orig);

  if (Builder.currentBB()) {
    auto* E1 = attr(1).Exp;
    auto* A = dyn_cast<Alloc>(E0);

    if (A && A->allocID() > 0 && A->allocID() < CurrentVarMap->size()) {
      // Remove the use that we marked for this store during traversal.
      assert(NumUses[A->instrID()] > 0);
      --NumUses[A->instrID()];

      // Update the map for the current block to hold the new value.
      assert(E1 && "Invalid store operation.");
      CurrentVarMap->at(A->allocID()) = E1;

      // Return future, which will delete the Store later if not needed.
      auto *F = new (FutArena) FutureStore(Orig, A);
      PendingStores.push_back(F);
      resultAttr().Exp = F;
    }
  }
}


void SSAPass::reduceLoad(Load *Orig) {
  // Alloc is rewritten to a future above, so grab the original
  auto* E0 = Orig->pointer();

  // Rewrite the load, in case we need it.
  Super::reduceLoad(Orig);

  if (Builder.currentBB()) {
    auto* A = dyn_cast<Alloc>(E0);

    if (A && A->allocID() > 0 && A->allocID() < CurrentVarMap->size()) {
      // Remove the use that we marked for this load during traversal.
      assert(NumUses[A->instrID()] > 0);
      --NumUses[A->instrID()];

      auto* Av = CurrentVarMap->at(A->allocID());
      if (Av) {
        if (isa<Undefined>(Av)) {
          // Load value is undefined -- keep the load and alloc.
          ++NumUses[A->instrID()];
          resultAttr().Exp = Orig;
        }
        else {
          // Replace load with value from current var map.
          resultAttr().Exp = Av;
        }
      }
      else {
        // Replace load with a future, which does lazy lookup.
        auto *F = new (FutArena) FutureLoad(Orig, A);
        PendingLoads.push_back(F);
        resultAttr().Exp = F;
      }
    }
  }
}



// This is the second pass of the SSA conversion, which looks up values for
// all loads, and replaces the loads.
void SSAPass::replacePending() {
  // Delete all unused Allocs for local variables
  for (auto *F : PendingAllocs) {
    auto* A = F->AllocInstr;
    if (NumUses[A->instrID()] <= 0) {
      F->setResult(nullptr);
    }
    else {
      if (A->isLocal()) {
        // TODO: warn on misuse of local variable.
        A->setAllocKind(Alloc::AK_Stack);
      }
      F->setResult(A);
    }
  }
  PendingAllocs.clear();

  // Delete all stores to unused Allocs.
  for (auto *F : PendingStores) {
    if (NumUses[F->AllocInstr->instrID()] <= 0)
      F->setResult(nullptr);
    else
      F->setResult(F->StoreInstr);
  }
  PendingStores.clear();


  // Second pass:  Go back and replace all loads with phi nodes or values.
  // CurrVarMapCache holds lookups that we've already done in the current block.
  BasicBlock* CurrBB = nullptr;
  LocalVarMap CurrVarMapCache;

  for (auto *F : PendingLoads) {
    Alloc *A = F->AllocInstr;

    if (NumUses[A->instrID()] > 0) {
      F->setResult(F->LoadInstr);   // Keep the load in place.
      continue;
    }

    BasicBlock *B = F->block();
    if (B != CurrBB) {
      // We've switched to a new block.  Clear the cache.
      CurrVarMapCache.clear();
      unsigned MSize = BInfoMap[B->blockID()].AllocVarMap.size();
      CurrVarMapCache.resize(MSize, nullptr);
      CurrBB = B;
    }
    auto  LvarID = A->allocID();
    auto* E = CurrVarMapCache[LvarID];
    if (!E) {
      E = lookupInPredecessors(B, LvarID, A->instrName());
      CurrVarMapCache[LvarID] = E;
    }
    if (E) {
      F->setResult(E);  // Replace load
    }
    else {
      // TODO: error on completely undefined variable.
      F->setResult(Builder.newUndefined());
    }
  }

  PendingAllocs.clear();
  PendingStores.clear();
  PendingLoads.clear();
}


SExpr* SSAPass::lookupInCache(LocalVarMap *LvarMap, unsigned LvarID) {
  if (LvarID >= LvarMap->size())
    return nullptr;

  SExpr *E = LvarMap->at(LvarID);
  if (!E)
    return nullptr;

  // The cached value may be an incomplete and temporary Phi node, that was
  // later eliminated. If so, grab the real value and update the cache.
  // Phi nodes may contain other phi nodes.
  do {
    auto *Ph = dyn_cast<Phi>(E);
    if (Ph && Ph->status() == Phi::PH_SingleVal) {
      E = Ph->values()[0].get();
      LvarMap->at(LvarID) = E;
      continue;
    }
    break;
  } while (true);

  // The cached value may be a Future that has since been resolved.
  // If so, grab the forced value and update the cache.
  // Futures may contain other futures.
  do {
    auto* Fut = dyn_cast_or_null<Future>(E);
    if (Fut && Fut->maybeGetResult()) {
      E = Fut->maybeGetResult();
      LvarMap->at(LvarID) = E;
      continue;
    }
    break;
  } while (true);

  return E;
}


Phi* SSAPass::makeNewPhiNode(unsigned i, SExpr *E, unsigned numPreds) {
  // Values don't match, so make a new phi node.
  auto *Ph = new (arena()) Phi(arena(), numPreds);
  // Fill it with the original value E
  for (unsigned j = 0; j < i; ++j)
    Ph->values().emplace_back(arena(), E);
  if (Instruction *I = dyn_cast_or_null<Instruction>(E))
    Ph->setBaseType(I->baseType());
  return Ph;
}


// Lookup value of local variable at the beginning of basic block B
SExpr* SSAPass::lookupInPredecessors(BasicBlock *B, unsigned LvarID,
                                     StringRef Nm) {
  assert(LvarID > 0 && "Invalid variable ID");

  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  if (LvarID >= LvarMap->size())
    return nullptr;  // Invalid CFG.

  SExpr* E  = nullptr;   // The first value we find.
  SExpr* E2 = nullptr;   // The second distinct value we find.
  Phi*   Ph = nullptr;   // The Phi node we created (if any)
  bool Incomplete = false;                // Is Ph incomplete?
  bool SetInBlock = LvarMap->at(LvarID);  // Is var set within this block?
  unsigned i = 0;

  for (auto &P : B->predecessors()) {
    if (!Ph && !SetInBlock && P->blockID() >= B->blockID()) {
      // This is a back-edge, and we didn't set the variable in this block.
      // Create a dummy Phi node to avoid infinite recursion before lookup.
      Ph = makeNewPhiNode(i, E, B->numPredecessors());
      Incomplete = true;
      LvarMap->at(LvarID) = Ph;
      SetInBlock = true;
    }

    // Any loads in the block that declares a variable have already been
    // eliminated.  The variable must have been declared in a dominating block.
    E2 = lookup(P.get(), LvarID, Nm);
    if (!E2) {
      // TODO: error on undefined variable. Should at least be Undefined.
      continue;
    }

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
      if (!SetInBlock) {
        LvarMap->at(LvarID) = Ph;
        SetInBlock = true;
      }
    }
    ++i;
  }

  if (Ph) {
    if (Incomplete) {
      assert(LvarMap->at(LvarID) == Ph && "Phi should have been cached.");
      // Replace Phi node in cache.
      LvarMap->at(LvarID) = E;

      // Ph may have been cached elsewhere, so mark it as single val.
      // It will be eliminated by lookupInCache/
      if (Ph->numValues() > 0) {
        SExprRef& Val0 = Ph->values()[0];
        if (Val0.get() != E) {
          // Don't reset if it's already E, because E may be a future.
          assert((Val0.get() == nullptr || Val0.get() == Ph) && "Invalid Phi");
          Val0.reset(E);
        }
      }
      else {
        Ph->values().emplace_back(arena(), E);
      }
      Ph->setStatus(Phi::PH_SingleVal);
    }
    else {
      // Valid Phi node; add it to the block and return it.
      E = Ph;
      Ph->setInstrName(Builder, Nm);
      B->addArgument(Ph);
    }
  }
  return E;
}


// Lookup value of local variable at the end of basic block B
SExpr* SSAPass::lookup(BasicBlock *B, unsigned LvarID, StringRef Nm) {
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  if (LvarID >= LvarMap->size())
    return nullptr;   // Invalid CFG.

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

