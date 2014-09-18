//===- CFGReducer.cpp ------------------------------------------*- C++ --*-===//
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

#include "CFGReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;


SExpr* VarContext::lookup(StringRef S) {
  for (unsigned i=0,n=Vars.size(); i < n; ++i) {
    VarDecl* V = Vars[n-i-1];
    if (V->name() == S) {
      return V;
    }
  }
  return nullptr;
}


void CFGRewriteReducer::startBlock(BasicBlock *BB) {
  assert(currentBB_ == nullptr && "Haven't finished current block.");
  assert(currentArgs_.empty());
  assert(currentInstrs_.empty());
  assert(BB->instructions().size() == 0 && "Already processed block.");

  currentBB_ = BB;
  if (!BB->cfg())
    currentCFG_->add(BB);
}


Branch* CFGRewriteReducer::createBranch(SExpr *Cond) {
  assert(currentBB_);

  // Create new basic blocks for then and else.
  BasicBlock *Ntb = addBlock();
  Ntb->addPredecessor(currentBB_);

  BasicBlock *Neb = addBlock();
  Neb->addPredecessor(currentBB_);

  // Terminate current basic block with a branch
  auto *Nt = new (Arena) Branch(Cond, Ntb, Neb);
  finishBlock(Nt);
  return Nt;
}


Goto* CFGRewriteReducer::createGoto(BasicBlock *Target, SExpr* Result) {
  assert(currentBB_);

  unsigned Idx = Target->addPredecessor(currentBB_);
  if (Target->arguments().size() > 0) {
    // First argument is always the result
    SExpr *E = Target->arguments()[0];
    if (Phi *Ph = dyn_cast<Phi>(E))
      Ph->values()[Idx] = Result;
  }
  auto *Nt = new (Arena) Goto(Target, Idx);
  finishBlock(Nt);
  return Nt;
}


BasicBlock* CFGRewriteReducer::initCFG() {
  assert(currentCFG_ == nullptr && currentBB_ == nullptr);
  currentCFG_ = new (Arena) SCFG(Arena, 0);
  currentBB_ = currentCFG_->entry();
  assert(currentBB_->instructions().size() == 0);
  return currentCFG_->exit();
}


SCFG* CFGRewriteReducer::finishCFG() {
  StdPrinter::print(currentCFG_, std::cout);
  std::cout << "\n\n";
  currentCFG_->computeNormalForm();
  return currentCFG_;
}


void CFGRewriteReducer::addLetDecl(VarDecl* Nv) {
  // Set the block and ID now, to mark it as having been added.
  Nv->setID(currentBB_, currentInstrNum_++);
  if (currentInstrs_.size() > 0 &&
      currentInstrs_.back() == Nv->definition() &&
      !isa<VarDecl>(Nv->definition())) {
    // Definition already in block -- replace old instr with let decl.
    Nv->definition()->setID(nullptr, 0);
    currentInstrs_.back() = Nv;
  }
  else {
    currentInstrs_.push_back(Nv);
  }
}


void CFGRewriteReducer::addInstruction(SExpr* E) {
  if (!ThreadSafetyTIL::isTrivial(E) && !E->block()) {
    // Set the block and ID now, to mark it as having been added.
    // We won't actually add instructions until the block is done.
    E->setID(currentBB_, currentInstrNum_++);
    currentInstrs_.push_back(E);
  }
}


BasicBlock* CFGRewriteReducer::addBlock() {
  BasicBlock *B = new (Arena) BasicBlock(Arena);
  B->setBlockID(currentBlockNum_++);
  return B;
}


void CFGRewriteReducer::finishBlock(Terminator* Term) {
  assert(currentBB_);
  assert(currentBB_->instructions().size() == 0);

  currentBB_->instructions().reserve(currentInstrs_.size(), Arena);
  for (auto *E : currentInstrs_) {
    currentBB_->addInstruction(E);
  }
  currentBB_->setTerminator(Term);
  currentArgs_.clear();
  currentInstrs_.clear();
  currentBB_ = nullptr;
}


BasicBlock* CFGRewriteReducer::makeContinuation() {
  auto *Ncb = addBlock();
  auto *Nph = new (Arena) Phi();
  Ncb->setID(Ncb, currentInstrNum_++);
  Ncb->addArgument(Nph);
  return Ncb;
}


SExpr* CFGRewriter::traverseIfThenElse(IfThenElse *E, CtxT Ctx,
                                       TraversalKind K) {
  if (!Ctx.insideCFG()) {
    // Just do a normal traversal if we're not currently rewriting in a CFG.
    return E->traverse(*this->self(), Ctx);
  }

  // Get the current continuation, or make one.
  CtxT Cont = Ctx.getCurrentContinuation();

  // End current block with a branch
  SExpr*  cond = E->condition();
  SExpr*  Nc = this->self()->traverse(&cond, Ctx);
  Branch* Br = Ctx->createBranch(Nc);

  // Process the then and else blocks
  SExpr* thenE = E->thenExpr();
  Cont->startBlock(Br->thenBlock());
  this->self()->traverse(&thenE, Cont, TRV_Tail);

  SExpr* elseE = E->elseExpr();
  Cont->startBlock(Br->elseBlock());
  this->self()->traverse(&elseE, Cont, TRV_Tail);

  // If we had an existing continuation, then we're done.
  // The then/else blocks will call the continuation.
  if (Ctx.continuation())
    return nullptr;

  // Otherwise, if we created a new continuation, then start processing it.
  Cont->startBlock(Cont.continuation());
  assert(Cont.continuation()->arguments().size() > 0);
  return Cont.continuation()->arguments()[0];
}


SCFG* CFGRewriter::convertSExprToCFG(SExpr *E, MemRegionRef A) {
  CFGRewriteReducer Reducer(A);
  CFGRewriter Traverser;

  auto *Exit = Reducer.initCFG();
  Traverser.traverse(&E, CtxT(&Reducer, Exit), TRV_Tail);
  return Reducer.finishCFG();
}


}  // end namespace ohmu

