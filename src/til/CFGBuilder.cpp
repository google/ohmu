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
  assert((CurrentBB->instructions().size() == 0 || OverwriteInstructions) &&
         "Already finished block.");

  // Ovewrite existing arguments with CurrentArgs, if requested.
  if (OverwriteArguments)
    CurrentBB->arguments().clear();

  if (CurrentArgs.size() > 0) {
    auto Sz = CurrentBB->arguments().size();
    CurrentBB->arguments().reserve(Arena, Sz + CurrentArgs.size());
    for (auto *E : CurrentArgs)
      CurrentBB->addArgument(E);
  }

  // Overwrite existing instructions with CurrentInstrs, if requested.
  if (OverwriteInstructions)
    CurrentBB->instructions().clear();

  if (CurrentInstrs.size() > 0) {
    auto Sz = CurrentBB->instructions().size();
    CurrentBB->instructions().reserve(Arena, Sz + CurrentInstrs.size());
    for (auto *E : CurrentInstrs)
      CurrentBB->addInstruction(E);
  }

  // Set the terminator, if one has been specified.
  if (Term)
    CurrentBB->setTerminator(Term);

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
    assert(B->arguments().size() == 1);
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

