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
      return V->definition();
    }
  }
  return nullptr;
}


void CFGRewriteReducer::enterScope(VarDecl *Orig, VarDecl *Nv) {
  if (Orig->name().length() > 0) {
    varCtx_.push(Nv);
    if (currentBB_)
      if (Instruction *I = Nv->definition()->asCFGInstruction())
        if (I->name().length() == 0)
          I->setName(Nv->name());
  }
}


void CFGRewriteReducer::exitScope(const VarDecl *Orig) {
  if (Orig->name().length() > 0) {
    assert(Orig->name() == varCtx_.back()->name() && "Variable mismatch");
    varCtx_.pop();
  }
}



void CFGRewriteReducer::addInstruction(SExpr* E) {
  if (E->isTrivial())
    return;

  if (Instruction* I = dyn_cast<Instruction>(E)) {
    if (!I->block()) {
      I->setID(currentBB_, currentInstrNum_++);
      currentInstrs_.push_back(I);
    }
  }
}


BasicBlock* CFGRewriteReducer::addBlock() {
  BasicBlock *B = new (Arena) BasicBlock(Arena);
  B->setBlockID(currentBlockNum_++);
  return B;
}


BasicBlock* CFGRewriteReducer::makeContinuation() {
  auto *Ncb = addBlock();
  auto *Nph = new (Arena) Phi();
  Nph->setID(Ncb, currentInstrNum_++);
  Ncb->addArgument(Nph);
  return Ncb;
}


void CFGRewriteReducer::startBlock(BasicBlock *BB, BasicBlock *Cont) {
  assert(currentBB_ == nullptr && "Haven't finished current block.");
  assert(currentArgs_.empty());
  assert(currentInstrs_.empty());
  assert(BB->instructions().size() == 0 && "Already processed block.");

  currentBB_ = BB;
  if (!BB->cfg())
    currentCFG_->add(BB);
  pushContinuation(Cont);
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


void CFGRewriteReducer::initCFG() {
  assert(currentCFG_ == nullptr && currentBB_ == nullptr);
  currentCFG_ = new (Arena) SCFG(Arena, 0);
  currentBB_ = currentCFG_->entry();
  pushContinuation(currentCFG_->exit());
  assert(currentBB_->instructions().size() == 0);
}


SCFG* CFGRewriteReducer::finishCFG() {
  StdPrinter::print(currentCFG_, std::cout);
  std::cout << "\n\n";
  currentCFG_->computeNormalForm();
  return currentCFG_;
}



SExpr* CFGRewriter::traverseIfThenElse(IfThenElse *E, CFGRewriteReducer *R,
                                       TraversalKind K) {
  if (!R->currentBB_) {
    // Just do a normal traversal if we're not currently rewriting in a CFG.
    return E->traverse(*this->self(), R);
  }

  SExpr* cond = E->condition();
  SExpr* Nc = this->self()->traverseDM(&cond, R);

  // End current block with a branch
  BasicBlock* CurrCont = R->popContinuation();
  Branch* Br = R->createBranch(Nc);

  // If the continuation is null, then make a new one.
  BasicBlock* Cont = CurrCont;
  if (!Cont)
    Cont = R->makeContinuation();

  // Process the then and else blocks
  SExpr* thenE = E->thenExpr();
  R->startBlock(Br->thenBlock(), Cont);
  this->self()->traverseDM(&thenE, R, TRV_Tail);

  SExpr* elseE = E->elseExpr();
  R->startBlock(Br->elseBlock(), Cont);
  this->self()->traverseDM(&elseE, R, TRV_Tail);

  // If we had an existing continuation, then we're done.
  // The then/else blocks will call the continuation.
  if (CurrCont)
    return nullptr;

  // Otherwise, if we created a new continuation, then start processing it.
  R->startBlock(Cont, nullptr);
  assert(Cont->arguments().size() > 0);
  return Cont->arguments()[0];
}


SCFG* CFGRewriter::convertSExprToCFG(SExpr *E, MemRegionRef A) {
  CFGRewriteReducer Reducer(A);
  CFGRewriter Traverser;

  Reducer.initCFG();
  Traverser.traverse(E, &Reducer, TRV_Tail);
  return Reducer.finishCFG();
}


}  // end namespace ohmu

