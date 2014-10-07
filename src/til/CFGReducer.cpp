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

#include "til/CFGReducer.h"
#include "til/InplaceReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;


VarDecl* VarContext::lookup(StringRef s) {
  for (unsigned i=0,n=vars_.size(); i < n; ++i) {
    VarDecl* vd = vars_[n-i-1];
    if (vd->name() == s) {
      return vd;
    }
  }
  return nullptr;
}


void CFGRewriteReducer::enterScope(VarDecl *orig, VarDecl *nv) {
  if (orig->name().length() > 0) {
    varCtx_->push(nv);
    if (currentBB_ && nv->definition())
      if (Instruction *I = nv->definition()->asCFGInstruction())
        if (I->name().length() == 0)
          I->setName(nv->name());
  }
}


void CFGRewriteReducer::exitScope(const VarDecl *orig) {
  if (orig->name().length() > 0) {
    assert(orig->name() == varCtx_->back()->name() && "Variable mismatch");
    varCtx_->pop();
  }
}



SExpr* CFGRewriteReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  // std::cerr << "Apply: ";
  // TILDebugPrinter::print(e, std::cerr);
  // std::cerr << "\n";

  if (auto* F = dyn_cast<Function>(e)) {
    pendingPathArgs_.push_back(a);
    return F->body();
  }

  // std::cerr << "Unhandled.\n";
  return CopyReducer::reduceApply(orig, e, a);
}


SExpr* CFGRewriteReducer::reduceCall(Call &orig, SExpr *e) {
  // std::cerr << "Call: ";
  // TILDebugPrinter::print(e, std::cerr);
  // std::cerr << "\n";

  // Traversing Apply and SApply will push arguments onto pendingPathArgs_.
  // A call expression will consume the args.
  if (auto* c = dyn_cast<Code>(e)) {
    // TODO: handle more than one arg.
    auto it = codeMap_.find(c);
    if (it != codeMap_.end()) {
      // This is a locally-defined function, which maps to a basic block.
      unsigned pi      = it->second;
      PendingBlock& pb = pendingBlocks_[pi];

      // All calls are tail calls.  Make a continuation if we don't have one.
      BasicBlock* cont = currentContinuation();
      if (!cont)
        cont = addBlock(1);

      // Set the continuation of the pending block to the current continuation.
      // If there are multiple calls, the continuations must match.
      if (pb.continuation)
        assert(pb.continuation == cont && "Cannot transform to tail call!");
      else
        pb.continuation = cont;

      // End current block with a jump to the new one.
      createGoto(pb.block, pendingPathArgs_);

      // Add the pending block to the queue of reachable blocks, which will
      // be rewritten later.
      pendingBlockQueue_.push(pi);
      pendingPathArgs_.clear();
      return nullptr;
    }
  }

  // std::cerr << "Unhandled.\n";

  SExpr *f = e;
  for (auto *a : pendingPathArgs_)
    f = new (Arena) Apply(f, a);
  return CopyReducer::reduceCall(orig, f);
}


SExpr* CFGRewriteReducer::reduceCode(Code& orig, SExpr* e0, SExpr* e1) {
  // Function arguments in the context will become phi nodes in the block.
  unsigned nargs = 0;
  unsigned sz = varCtx_->size();
  while (nargs < sz && (*varCtx_)[nargs]->kind() == VarDecl::VK_Fun)
    ++nargs;

  // TODO: right now, we assume that all local functions will become blocks.
  // Eventually, we'll need to handle proper nested lambdas.

  // Create a new block.
  // Clone the current context, but replace function parameters with phi nodes
  // in the new block.
  BasicBlock *b = addBlock(nargs);
  VarContext* nvc = varCtx_->clone();
  for (unsigned i = 0; i < nargs; ++i) {
    unsigned j   = nargs-1-i;
    StringRef nm = (*varCtx_)[j]->name();
    b->arguments()[i]->setName(nm);
    (*nvc)[j] = new (Arena) VarDecl(nm, b->arguments()[i]);
  }

  // Add the new blocks to the pending blocks array.
  unsigned pi = pendingBlocks_.size();
  pendingBlocks_.push_back(PendingBlock(orig.body(), b, nvc));

  // Create a code expr, and add it to the code map.
  Code* c = CopyReducer::reduceCode(orig, e0, nullptr);
  codeMap_.insert(std::make_pair(c, pi));
  return c;
}


SExpr* CFGRewriteReducer::reduceIdentifier(Identifier &orig) {
  VarDecl* vd = varCtx_->lookup(orig.name());
  // TODO: emit warning on name-not-found.
  if (vd) {
    if (vd->kind() == VarDecl::VK_Let ||
        vd->kind() == VarDecl::VK_Letrec)
      return vd->definition();
    return new (Arena) Variable(vd);
  }
  return new (Arena) Identifier(orig);
}


SExpr* CFGRewriteReducer::reduceLet(Let &orig, VarDecl *nvd, SExpr *b) {
  if (currentCFG_)
    return b;   // eliminate the let
  else
    return CopyReducer::reduceLet(orig, nvd, b);
}



void CFGRewriteReducer::addInstruction(SExpr* e) {
  switch (e->opcode()) {
    case COP_Literal:  return;
    case COP_Variable: return;
    case COP_Apply:    return;
    case COP_SApply:   return;
    case COP_Project:  return;
    default: break;
  }

  if (Instruction* i = dyn_cast<Instruction>(e)) {
    if (!i->block()) {
      i->setID(currentBB_, currentInstrNum_++);
      currentInstrs_.push_back(i);
    }
  }
}


BasicBlock* CFGRewriteReducer::addBlock(unsigned nargs) {
  BasicBlock *b = new (Arena) BasicBlock(Arena);
  b->setBlockID(currentBlockNum_++);
  for (unsigned i = 0; i < nargs; ++i) {
    auto *ph = new (Arena) Phi();
    ph->setID(b, currentInstrNum_++);
    b->addArgument(ph);
  }
  return b;
}


void CFGRewriteReducer::startBlock(BasicBlock *bb) {
  assert(currentBB_ == nullptr && "Haven't finished current block.");
  assert(currentArgs_.empty());
  assert(currentInstrs_.empty());
  assert(bb->instructions().size() == 0 && "Already processed block.");

  currentBB_ = bb;
  if (!bb->cfg())
    currentCFG_->add(bb);
}


void CFGRewriteReducer::finishBlock(Terminator* term) {
  assert(currentBB_);
  assert(currentBB_->instructions().size() == 0);

  currentBB_->instructions().reserve(currentInstrs_.size(), Arena);
  for (auto *E : currentInstrs_) {
    currentBB_->addInstruction(E);
  }
  currentBB_->setTerminator(term);
  currentArgs_.clear();
  currentInstrs_.clear();
  currentBB_ = nullptr;
}


Branch* CFGRewriteReducer::createBranch(SExpr *cond) {
  assert(currentBB_);

  // Create new basic blocks for then and else.
  BasicBlock *ntb = addBlock();
  ntb->addPredecessor(currentBB_);

  BasicBlock *neb = addBlock();
  neb->addPredecessor(currentBB_);

  // Terminate current basic block with a branch
  auto *nt = new (Arena) Branch(cond, ntb, neb);
  finishBlock(nt);
  return nt;
}


Goto* CFGRewriteReducer::createGoto(BasicBlock *target, SExpr* result) {
  assert(currentBB_);
  assert(target->arguments().size() == 1);

  unsigned idx = target->addPredecessor(currentBB_);
  Phi *ph = target->arguments()[0];
  ph->values()[idx] = result;

  auto *nt = new (Arena) Goto(target, idx);
  finishBlock(nt);
  return nt;
}


Goto* CFGRewriteReducer::createGoto(BasicBlock *target,
                                    std::vector<SExpr*>& args) {
  assert(currentBB_);
  if (target->arguments().size() != args.size()) {
    std::cerr << "target: " << target->arguments().size()
              << " goto: "   << args.size() << "\n";
    assert(false);
  }

  unsigned idx = target->addPredecessor(currentBB_);
  for (unsigned pi = 0; pi < args.size(); ++pi) {
    Phi *ph = target->arguments()[pi];
    ph->values()[idx] = args[pi];
  }

  auto *nt = new (Arena) Goto(target, idx);
  finishBlock(nt);
  return nt;
}


void CFGRewriteReducer::initCFG() {
  assert(currentCFG_ == nullptr && currentBB_ == nullptr);
  currentCFG_ = new (Arena) SCFG(Arena, 0);
  currentBB_ = currentCFG_->entry();
  pushContinuation(currentCFG_->exit());
  assert(currentBB_->instructions().size() == 0);
}


SCFG* CFGRewriteReducer::finishCFG() {
  TILDebugPrinter::print(currentCFG_, std::cout);
  std::cout << "\n\n";
  popContinuation();
  currentCFG_->computeNormalForm();
  return currentCFG_;
}




SExpr* CFGRewriter::traverseIfThenElse(IfThenElse *e, CFGRewriteReducer *r,
                                       TraversalKind k) {
  if (!r->currentBB_) {
    // Just do a normal traversal if we're not currently rewriting in a CFG.
    return e->traverse(*this->self(), r);
  }

  // End current block with a branch
  SExpr* cond = e->condition();
  SExpr* nc = this->self()->traverseDM(&cond, r);
  Branch* br = r->createBranch(nc);

  // If the current continuation is null, then make a new one.
  BasicBlock* currCont = r->currentContinuation();
  BasicBlock* cont = currCont;
  if (!cont)
    cont = r->addBlock(1);

  // Process the then and else blocks
  SExpr* thenE = e->thenExpr();
  r->startBlock(br->thenBlock());
  r->pushContinuation(cont);
  this->self()->traverseDM(&thenE, r, TRV_Tail);
  r->popContinuation();

  SExpr* elseE = e->elseExpr();
  r->startBlock(br->elseBlock());
  r->pushContinuation(cont);
  this->self()->traverseDM(&elseE, r, TRV_Tail);
  r->popContinuation();

  // If we had an existing continuation, then we're done.
  // The then/else blocks will call the continuation.
  if (currCont)
    return nullptr;

  // Otherwise, if we created a new continuation, then start processing it.
  r->startBlock(cont);
  assert(cont->arguments().size() > 0);
  return cont->arguments()[0];
}


SCFG* CFGRewriter::convertSExprToCFG(SExpr *e, MemRegionRef a) {
  CFGRewriteReducer reducer(a);
  CFGRewriter traverser;

  reducer.initCFG();
  traverser.traverse(e, &reducer, TRV_Tail);
  reducer.finishLazyBlocks(traverser);
  return reducer.finishCFG();
}


}  // end namespace ohmu

