//===- CFGBuilder.h --------------------------------------------*- C++ --*-===//
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


#include "til/CFGBuilder.h"


namespace ohmu {

using namespace clang::threadSafety::til;


SCFG* CFGBuilder::beginSCFG(SCFG *Cfg, unsigned NumBlocks, unsigned NumInstrs) {
  assert(!CurrentCFG && !CurrentBB && "Already inside a CFG");
  assert(InstructionMap.empty());
  assert(BlockMap.empty());

  if (!Cfg) {
    CurrentCFG = new (Arena) SCFG(Arena, 0);
    // CurrentCFG->blocks().reserve(NumBlocks);
  }
  else {
    CurrentCFG = Cfg;
    NumBlocks = Cfg->numBlocks();
    NumInstrs = Cfg->numInstructions();
  }

  BlockMap.resize(NumBlocks, nullptr);
  InstructionMap.resize(NumInstrs, nullptr);
  return CurrentCFG;
}


void CFGBuilder::endSCFG() {
  assert(CurrentCFG && "Not inside a CFG.");
  // assert(!CurrentBB && "Never finished the last block.");

  CurrentCFG = nullptr;
  BlockMap.clear();
  InstructionMap.clear();
}



void CFGBuilder::beginBlock(BasicBlock *B) {
  assert(!CurrentBB && "Haven't finished current block.");
  assert(CurrentArgs.empty());
  assert(CurrentInstrs.empty());

  CurrentBB = B;
  if (!B->cfg())
    CurrentCFG->add(B);
}


void CFGBuilder::endBlock(Terminator *Term) {
  assert(CurrentBB && "No current block.");
  assert(OverwriteInstructions || CurrentBB->instructions().size() == 0 &&
         "Already finished block.");

  // Ovewrite existing arguments with CurrentArgs, if requested.
  if (OverwriteArguments)
    CurrentBB->arguments().clear();

  if (CurrentArgs.size() > 0) {
    auto Sz = CurrentBB->arguments().size();
    CurrentBB->arguments().reserve(Sz + CurrentArgs.size(), Arena);
    for (auto *E : CurrentArgs) {
      CurrentBB->addArgument(E);
    }
  }

  // Overwrite existing instructions with CurrentInstrs, if requested.
  if (OverwriteInstructions)
    CurrentBB->instructions().clear();

  if (CurrentInstrs.size() > 0) {
    auto Sz = CurrentBB->instructions().size();
    CurrentBB->instructions().reserve(Sz + CurrentInstrs.size(), Arena);
    for (auto *E : CurrentInstrs) {
      CurrentBB->addInstruction(E);
      if (Future *F = dyn_cast<Future>(E))
        handleFutureInstr(&CurrentBB->instructions().back(), F);
    }
  }
  CurrentBB->setTerminator(Term);

  CurrentArgs.clear();
  CurrentInstrs.clear();
  CurrentBB = nullptr;
}


/// Add block to blockmap, and add its arguments to InstructionMap.
void CFGBuilder::mapBlock(BasicBlock *B, BasicBlock *B2) {
  // Create a map from B1 to B2
  BlockMap[B->blockID()] = B2;

  // Create a map from the arguments of B1 to the arguments of B2
  unsigned Nargs = B->arguments().size();
  assert(Nargs == B2->arguments().size() && "Block arguments don't match.");

  for (unsigned i = 0; i < Nargs; ++i) {
    Phi *Ph = B->arguments()[i];
    if (Ph && Ph->instrID() > 0)
      InstructionMap[Ph->instrID()] = B2->arguments()[i];
  }
}



BasicBlock* CFGBuilder::newBlock(unsigned Nargs, unsigned Npreds) {
  BasicBlock *B = new (Arena) BasicBlock(Arena);
  if (Nargs > 0) {
    B->predecessors().reserve(Npreds, Arena);
    B->arguments().reserve(Nargs, Arena);
    for (unsigned i = 0; i < Nargs; ++i) {
      auto *Ph = new (Arena) Phi();
      Ph->values().reserve(Npreds, Arena);
      B->addArgument(Ph);
    }
  }
  return B;
}


Branch* CFGBuilder::newBranch(SExpr *Cond, BasicBlock *B0, BasicBlock *B1) {
  assert(CurrentBB && "No current block.");

  if (!B0)
    B0 = newBlock();
  if (!B1)
    B1 = newBlock();

  assert(B0->arguments().size() == 0 && "Cannot branch to a block with args.");
  assert(B1->arguments().size() == 0 && "Cannot branch to a block with args.");

  B0->addPredecessor(CurrentBB);
  B1->addPredecessor(CurrentBB);

  // Terminate current basic block with a branch
  auto *Nt = new (Arena) Branch(Cond, B0, B1);
  endBlock(Nt);
  return Nt;
}


Goto* CFGBuilder::newGoto(BasicBlock *B, SExpr* Result) {
  assert(CurrentBB && "No current block.");

  unsigned Idx = B->addPredecessor(CurrentBB);
  if (Result) {
    assert(B->arguments().size() == 1);
    Phi *Ph = B->arguments()[0];
    Ph->values()[Idx] = Result;
  }

  auto *Nt = new (Arena) Goto(B, Idx);
  endBlock(Nt);
  return Nt;
}


Goto* CFGBuilder::newGoto(BasicBlock *B, ArrayRef<SExpr*> Args) {
  assert(CurrentBB && "No current block.");
  assert(B->arguments().size() == Args.size() && "Wrong number of args.");

  unsigned Idx = B->addPredecessor(CurrentBB);
  for (unsigned i = 0, n = Args.size(); i < n; ++i) {
    Phi *Ph = B->arguments()[i];
    Ph->values()[Idx] = Args[i];
  }

  auto *Nt = new (Arena) Goto(B, Idx);
  endBlock(Nt);
  return Nt;
}


// This is a really a reducer method, not a builder method,
// But it's shared between CopyReducer and InplaceReducer.
void CFGBuilder::rewritePhiArg(Phi &Orig, Goto *NG, SExpr *Res) {
  // First find whatever Ph was rewritten to.
  Phi *Ph = dyn_cast_or_null<Phi>(InstructionMap[Orig.instrID()]);
  if (Ph && Ph->block() == NG->targetBlock()) {
    // The blocks match, so we know that Orig was rewritten to Ph.
    // (It might have been eliminated by rewriting to something else.)
    unsigned j = NG->phiIndex();
    Ph->values().resize(j+1, Arena, nullptr);  // Make room if we need to.
    if (!OverwriteInstructions)
      assert(!Ph->values()[j] && "We already handled this node.");
    Ph->values()[j] = Res;                     // Write the argument into Ph
    if (Future *F = dyn_cast<Future>(Res))
      handleFuturePhiArg(&Ph->values()[j], F);
  }
}


}  // end namespace ohmu
