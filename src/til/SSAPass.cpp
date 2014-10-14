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


SCFG* SSAPass::reduceSCFGBegin(SCFG &Orig) {
  assert(CurrentCFG == nullptr && "Already in a CFG.");
  CurrentCFG = &Orig;
  BInfoMap.resize(CurrentCFG->numBlocks());
  InstructionMap.resize(CurrentCFG->numInstructions(), nullptr);
  CurrentInstrID = 0;
  return &Orig;
}


SCFG* SSAPass::reduceSCFG(SCFG* Scfg) {
  assert(Scfg == CurrentCFG && "Internal traversal error.");

  // Second pass:  Go back and replace all loads with phi nodes or values.
  CurrentBB = Scfg->entry();
  for (PendingFuture &PF : Pending) {
    auto *F = PF.Fut;
    SExpr *E = F->maybeGetResult();
    if (!E) {
      Load  *L = F->LoadInstr;
      Alloc *A = F->AllocInstr;
      BasicBlock *B = L->block();
      if (B != CurrentBB) {
        // We've switched to a new block.  Clear the cache.
        CachedVarMap.clear();
        unsigned MSize = BInfoMap[B->blockID()].AllocVarMap.size();
        CachedVarMap.resize(MSize, nullptr);
        CurrentBB = B;
      }
      E = lookupInPredecessors(L->block(), A->allocID());
      F->setResult(E);
    }
    assert(PF.IPos && "Unhandled Future.");

    Instruction* I2 = cast<Instruction>(E);
    if (PF.INum > 0) {
      // If I2 is a new, non-trivial instruction, then replace the old one.
      // Otherwise eliminate the old instruction.
      if (I2->instrID() == 0 && !I2->isTrivial() && !isa<Phi>(I2))
        *PF.IPos = I2;
      else
        *PF.IPos = nullptr;
    }
    else {
      *PF.IPos = I2;
    }
  }
  CachedVarMap.clear();

  // TODO: clear future arena.
  Pending.clear();
  BInfoMap.clear();
  InstructionMap.clear();
  CurrentCFG = nullptr;

  // Assign numbers to phi nodes.
  Scfg->renumber();
  return Scfg;
}


BasicBlock* SSAPass::reduceBasicBlockBegin(BasicBlock &Orig) {
  assert(CurrentBB == nullptr && "Already in a basic block.");
  CurrentBB = &Orig;

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

BasicBlock* SSAPass::reduceBasicBlock(BasicBlock *BB) {
  assert(CurrentBB == BB && "Internal traversal error.");
  CurrentBB = nullptr;
  return BB;
}


void SSAPass::reduceBBArgument(BasicBlock *BB, unsigned i, SExpr* E) {
  // Get ID of the original argument.
  unsigned ID = BB->arguments()[i]->instrID();
  assert(CurrentInstrID == 0 || ID > CurrentInstrID &&
         "Instructions not numbered properly.");
  CurrentInstrID = ID;

  if (Phi* Ph = dyn_cast_or_null<Phi>(E)) {
    // Delete invalid Phi nodes.
    for (auto *A : Ph->values()) {
      if (!A) {
        Ph = nullptr;
        break;
      }
    }

    // Store E in instruction map and rewrite.
    InstructionMap[ID] = Ph;
    BB->arguments()[i] = Ph;
    return;
  }
  BB->arguments()[i] = nullptr;
}

// This is called instead of handleResult for basic block instructions.
void SSAPass::reduceBBInstruction(BasicBlock *BB, unsigned i, SExpr* E) {
  // Get the ID of the original instruction.
  unsigned ID = BB->instructions()[i]->instrID();
  assert(CurrentInstrID == 0 || ID > CurrentInstrID &&
         "Instructions not numbered properly.");
  CurrentInstrID = ID;

  // Warning: adding new instructions will invalidate Pending.
  if (Future *F = dyn_cast_or_null<Future>(E))
    Pending.push_back(PendingFuture(F, &BB->instructions()[i], ID));

  if (Instruction *I = dyn_cast_or_null<Instruction>(E)) {
    // Store E in instruction map and rewrite.
    InstructionMap[ID] = I;

    if (I == BB->instructions()[i])
      return;  // No change.

    // If this is a new, non-trivial instruction, replace the old one.
    // Otherwise eliminate the old instruction
    if (I->instrID() == 0 && !I->isTrivial() && !isa<Phi>(I))
      BB->instructions()[i] = I;        // rewrite
    else
      BB->instructions()[i] = nullptr;  // eliminate
    return;
  }
  BB->instructions()[i] = nullptr;
}


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


SExpr* SSAPass::reduceGoto(Goto &Orig, BasicBlock *B) {
  if (B->blockID() < Orig.block()->blockID()) {
    // FIXME!
    // This is a back-edge, so we need to reduce the Phi arguments that we
    // skipped earlier.
  }
  return &Orig;
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
  SExpr* E = nullptr;
  if (B == CurrentBB) {
    // See if we have a cached value in the current block.
    E = CachedVarMap[LvarID];
    if (E)
      return E;
  }

  // E is the first value we find, and E2 is the second.
  SExpr* E2 = nullptr;
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  Phi* Ph = nullptr;
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
      if (auto* E3 = LvarMap->at(LvarID)) {
        // Cached value may be a temporary Phi node.
        if (auto *Ph = dyn_cast<Phi>(E3)) {
          if (Ph->status() == Phi::PH_SingleVal) {
            E3 = Ph->values()[0];
            LvarMap->at(LvarID) = E3;
          }
        }
        return E3;
      }
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
      Ph->values()[0] = E;
      Ph->setStatus(Phi::PH_SingleVal);
    }
    else {
      E = Ph;
      B->addArgument(Ph);
    }
  }

  // Cache the result to avoid creating duplicate phi nodes.
  if (B == CurrentBB)
    CachedVarMap[LvarID] = E;
  return E;
}

// Lookup value of local variable at the end of basic block B
SExpr* SSAPass::lookup(BasicBlock *B, unsigned LvarID) {
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  assert(LvarID < LvarMap->size());
  // Check to see if the variable was set in this block.
  if (auto* E = LvarMap->at(LvarID)) {
    // Cached value may be a temporary Phi node.
    if (auto *Ph = dyn_cast<Phi>(E)) {
      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        LvarMap->at(LvarID) = E;
      }
    }
    return E;
  }
  // Lookup variable in predecessor blocks.
  auto* E = lookupInPredecessors(B, LvarID);
  // Cache the result.
  LvarMap->at(LvarID) = E;
  return E;
}


}  // end namespace ohmu

