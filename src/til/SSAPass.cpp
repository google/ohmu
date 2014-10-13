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
  for (PendingFuture &PF : Pending) {
    auto *F = PF.Fut;
    SExpr *E = F->maybeGetResult();
    if (!E) {
      Load  *L = F->LoadInstr;
      Alloc *A = F->AllocInstr;
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
      return nullptr;
    }
  }
  return &Orig;
}


SExpr* SSAPass::reduceLoad(Load &Orig, SExpr* E0) {
  if (CurrentBB) {
    // Replace load with value from current var map.
    if (auto* A = dyn_cast<Alloc>(E0)) {
      if (auto *Av = CurrentVarMap->at(A->allocID()))
        return Av;
      else
        return new (FutArena) FutureLoad(&Orig, A);
    }
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
  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
  SExpr* E = nullptr;
  Phi* Ph = nullptr;
  unsigned i = 0;  for (auto* P : B->predecessors()) {    SExpr* E2;    if (Ph) {      // We already have a phi node, so just copy E2 into it.      E2 = lookup(P, LvarID);      Ph->values().push_back(E2);    }    else if (P->blockID() >= B->blockID() && !LvarMap->at(LvarID)) {      // This is a back-edge, and we don't set the variable in this block.      // Create a dummy Phi node to avoid infinite recursion before lookup.      Ph = makeNewPhiNode(i, E, B->numPredecessors());      LvarMap->at(LvarID) = Ph;      E2 = lookup(P, LvarID);      Ph->values().push_back(E2);    }    else {      // Ordinary edge, so we do the lookup first, and create a Phi only      // if necessary.      E2 = lookup(P, LvarID);      if (E && E2 != E) {        // Values don't match, so we need a phi node.        Ph = makeNewPhiNode(i, E, B->numPredecessors());        Ph->values().push_back(E2);      }    }    E = E2;    ++i;  }  if (Ph) {    E = Ph;    B->addArgument(Ph);  }  // FIXME -- cache value at beginning of block.  return E;}
// Lookup value of local variable at the end of basic block BSExpr* SSAPass::lookup(BasicBlock *B, unsigned LvarID) {  auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;  assert(LvarID < LvarMap->size());  // Check to see if the variable was set in this block.  if (auto* E = LvarMap->at(LvarID))    return E;  // Lookup variable in predecessor blocks, and store in the end map.  auto* E = lookupInPredecessors(B, LvarID);  LvarMap->at(LvarID) = E;  return E;}


}  // end namespace ohmu

