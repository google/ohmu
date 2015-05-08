//===- CFGBuilder.cpp ------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//


#include "CFGBuilder.h"


namespace ohmu {
namespace til  {


void CFGBuilder::enterScope(VarDecl *Nvd) {
  assert(Nvd->varIndex() == 0 || Nvd->varIndex() == CurrentState.DeBruin);
  Nvd->setVarIndex(CurrentState.DeBruin);

  if (CurrentState.EmitInstrs) {
    // We are entering a function nested within a CFG.
    // Stop emitting instructions to the current CFG, and mark the spot.
    // Nested functions will be converted to blocks.
    OldCfgState = CurrentState;
    CurrentState.EmitInstrs = false;
  }
  ++CurrentState.DeBruin;
}


void CFGBuilder::exitScope() {
  --CurrentState.DeBruin;
  if (CurrentState.DeBruin == OldCfgState.DeBruin) {
    // We are exiting the nested function; return to CFG.
    CurrentState = OldCfgState;
    OldCfgState  = BuilderState(0, false);
  }
}



SCFG* CFGBuilder::beginCFG(SCFG *Cfg, unsigned NumBlocks, unsigned NumInstrs) {
  assert(!CurrentCFG && !CurrentBB && "Already inside a CFG");

  CurrentState.EmitInstrs = true;
  if (Cfg) {
    CurrentCFG = Cfg;
    return Cfg;
  }

  CurrentCFG = new (Arena) SCFG(Arena, 0);

  auto* Entry = new (Arena) BasicBlock(Arena);
  auto* Exit  = new (Arena) BasicBlock(Arena);
  auto *V     = new (Arena) Phi();
  auto *Ret   = new (Arena) Return(V);

  Exit->addArgument(V);
  Exit->setTerminator(Ret);
  Entry->setBlockID(0);
  Exit->setBlockID(1);

  CurrentCFG->add(Entry);
  CurrentCFG->add(Exit);
  CurrentCFG->setEntry(Entry);
  CurrentCFG->setExit(Exit);

  return CurrentCFG;
}


void CFGBuilder::endCFG() {
  assert(CurrentCFG && "Not inside a CFG.");
  // assert(!CurrentBB && "Never finished the last block.");

  CurrentCFG->renumber();
  CurrentState.EmitInstrs = false;
  CurrentCFG = nullptr;
}



void CFGBuilder::beginBlock(BasicBlock *B, bool Overwrite) {
  assert(!CurrentBB && "Haven't finished current block.");
  assert(CurrentArgs.empty());
  assert(CurrentInstrs.empty());

  CurrentBB = B;
  if (!B->cfg())
    CurrentCFG->add(B);

  // Mark existing instructions as "removed".
  // We don't remove them yet, because a rewriter will need to traverse them.
  // They will be cleared from the block when endBlock() is called.
  if (Overwrite) {
    for (auto& A : CurrentBB->arguments())
      A->setBlock(nullptr);
    for (auto& I : CurrentBB->instructions())
      I->setBlock(nullptr);
    if (CurrentBB->terminator())
      CurrentBB->terminator()->setBlock(nullptr);
    OverwriteCurrentBB = true;
  }
  else {
    OverwriteCurrentBB = false;
  }
}


void CFGBuilder::endBlock(Terminator *Term) {
  assert(CurrentBB && "No current block.");

  // Remove existing instructions if overwrite was requested in beginBlock.
  if (OverwriteCurrentBB) {
    CurrentBB->arguments().clear();
    CurrentBB->instructions().clear();
    OverwriteCurrentBB = false;
  }

  // Add new arguments to block.
  if (CurrentArgs.size() > 0) {
    auto Sz = CurrentBB->arguments().size();
    CurrentBB->arguments().reserve(Arena, Sz + CurrentArgs.size());
    for (auto *E : CurrentArgs)
      CurrentBB->addArgument(E);
  }

  // Add new instructions to block.
  if (CurrentInstrs.size() > 0) {
    auto Sz = CurrentBB->instructions().size();
    CurrentBB->instructions().reserve(Arena, Sz + CurrentInstrs.size());
    for (auto *E : CurrentInstrs)
      CurrentBB->addInstruction(E);
  }

  // Set the terminator, if one has been specified.
  if (Term) {
    Term->setBlock(CurrentBB);
    CurrentBB->setTerminator(Term);
  }

  CurrentArgs.clear();
  CurrentInstrs.clear();
  CurrentBB = nullptr;
}



BasicBlock* CFGBuilder::newBlock(unsigned Nargs, unsigned Npreds) {
  BasicBlock *B = new (Arena) BasicBlock(Arena);
  if (Nargs > 0) {
    B->predecessors().reserve(Arena, Npreds);
    B->arguments().reserve(Arena, Nargs);
    for (unsigned i = 0; i < Nargs; ++i) {
      auto *Ph = new (Arena) Phi();
      Ph->values().reserve(Arena, Npreds);
      B->addArgument(Ph);
    }
  }
  return B;
}


Branch* CFGBuilder::newBranch(SExpr *Cond, BasicBlock *B0, BasicBlock *B1) {
  assert(CurrentBB && "No current block.");

  if (B0) {
    assert(B0->numArguments() == 0 && "Cannot branch to a block with args.");
    B0->addPredecessor(CurrentBB);
  }
  if (B1) {
    assert(B1->numArguments() == 0 && "Cannot branch to a block with args.");
    B1->addPredecessor(CurrentBB);
  }

  // Terminate current basic block with a branch
  auto *Nt = new (Arena) Branch(Cond, B0, B1);
  endBlock(Nt);
  return Nt;
}


void CFGBuilder::setPhiArgument(Phi* Ph, SExpr* E, unsigned Idx) {
  if (!E)
    return;

  Instruction *I = dyn_cast<Instruction>(E);
  if (!I) {
    Diag.error("Invalid argument to Phi node: ") << E;
    return;
  }

  Ph->values().resize(Arena, Idx+1, nullptr);  // Make room if we need to.
  Ph->values()[Idx].reset(I);

  // Futures don't yet have types...
  // TODO: We could wind up with untyped phi nodes.
  if (isa<Future>(I))
    return;

  // Update the type of the Phi node.
  // All phi arguments must have the exact same type.
  if (Idx == 0 && Ph->baseType().Base == BaseType::BT_Void) {
    // Set the initial type of the Phi node.
    Ph->setBaseType(I->baseType());
  }
  else if (Ph->baseType() != I->baseType()) {
    Diag.error("Type mismatch in branch: ")
      << I << " does not have type " << Ph->baseType().getTypeName();
  }
}


Goto* CFGBuilder::newGoto(BasicBlock *B, SExpr* Result) {
  assert(CurrentBB && "No current block.");

  unsigned Idx = B->addPredecessor(CurrentBB);
  if (Result) {
    assert(B->arguments().size() == 1 && "Block has no arguments.");
    Phi *Ph = B->arguments()[0];
    setPhiArgument(Ph, Result, Idx);
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
    setPhiArgument(Ph, Args[i], Idx);
  }

  auto *Nt = new (Arena) Goto(B, Idx);
  endBlock(Nt);
  return Nt;
}


}  // end namespace til
}  // end namespace ohmu

