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
#include "til/SSAPass.h"

namespace ohmu {

using namespace clang::threadSafety::til;


class CFGCopier : public CopyReducer,
                  public DefaultScopeHandler<SExprReducerMap>,
                  public Traversal<CFGCopier, SExprReducerMap> {
public:
  CFGCopier(MemRegionRef a) : CopyReducer(a) { }

  static SExpr* copy(SExpr* e, MemRegionRef a) {
    CFGCopier copier(a);
    return copier.traverse(e, TRV_Tail);
  }
};




VarDecl* VarContext::lookup(StringRef s) {
  for (unsigned i=0,n=vars_.size(); i < n; ++i) {
    VarDecl* vd = vars_[n-i-1];
    if (vd->name() == s) {
      return vd;
    }
  }
  return nullptr;
}


void CFGReducer::enterScope(VarDecl *orig, VarDecl *nv) {
  if (orig->name().length() > 0) {
    varCtx_->push(nv);
    if (currentBB() && nv->definition())
      if (Instruction *I = dyn_cast<Instruction>(nv->definition()))
        if (I->name().length() == 0)
          I->setName(nv->name());
  }
}


void CFGReducer::exitScope(const VarDecl *orig) {
  if (orig->name().length() > 0) {
    assert(orig->name() == varCtx_->back()->name() && "Variable mismatch");
    varCtx_->pop();
  }
}


SExpr* CFGReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  if (auto* F = dyn_cast<Function>(e)) {
    pendingPathArgs_.push_back(a);
    return F->body();
  }
  return CopyReducer::reduceApply(orig, e, a);
}


SExpr* CFGReducer::reduceCall(Call &orig, SExpr *e) {
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
        cont = newBlock(1);

      // Set the continuation of the pending block to the current continuation.
      // If there are multiple calls, the continuations must match.
      if (pb.continuation)
        assert(pb.continuation == cont && "Cannot transform to tail call!");
      else
        pb.continuation = cont;

      // End current block with a jump to the new one.
      unsigned nargs = numPendingArgs();
      SExpr** pbegin = &pendingPathArgs_[pendingPathArgs_.size()-nargs];
      newGoto(pb.block, ArrayRef<SExpr*>(pbegin, nargs));

      // Add the pending block to the queue of reachable blocks, which will
      // be rewritten later.
      pendingBlockQueue_.push(pi);
      for (unsigned i = 0; i < nargs; ++i)
        pendingPathArgs_.pop_back();

      // If this was a newly-created continuation, then continue where we
      // left off.
      if (!currentContinuation()) {
        beginBlock(cont);
        return cont->arguments()[0];
      }
      return nullptr;
    }
  }

  SExpr *f = e;
  for (auto *a : pendingPathArgs_)
    f = new (Arena) Apply(f, a);
  return CopyReducer::reduceCall(orig, f);
}


SExpr* CFGReducer::reduceCode(Code& orig, SExpr* e0, SExpr* e1) {
  if (!currentCFG())
    return CopyReducer::reduceCode(orig, e0, e1);

  // Code blocks inside a CFG will be lowered to basic blocks.

  // Function arguments in the context will become phi nodes in the block.
  unsigned nargs = 0;
  unsigned sz = varCtx_->size();
  while (nargs < sz && (*varCtx_)[nargs]->kind() == VarDecl::VK_Fun)
    ++nargs;

  // TODO: right now, we assume that all local functions will become blocks.
  // Eventually, we'll need to handle proper nested lambdas.

  // Create a new block.
  BasicBlock *b = newBlock(nargs);
  // Clone the current context, but replace function parameters with phi nodes
  // in the new block.
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


SExpr* CFGReducer::reduceIdentifier(Identifier &orig) {
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


SExpr* CFGReducer::reduceLet(Let &orig, VarDecl *nvd, SExpr *b) {
  if (currentCFG())
    return b;   // eliminate the let
  else
    return CopyReducer::reduceLet(orig, nvd, b);
}


SExpr* CFGReducer::traverseCode(Code* e, TraversalKind k) {
  auto Nt = self()->traverse(e->returnType(), TRV_Type);
  SExpr *Nb = nullptr;
  if (!currentCFG()) {
    beginSCFG(nullptr);
    self()->traverse(e->body(), TRV_Tail);
    Nb = currentCFG();
    endSCFG();
  }
  return self()->reduceCode(*e, Nt, Nb);
}


SExpr* CFGReducer::traverseIfThenElse(IfThenElse *e, TraversalKind k) {
  if (!currentBB()) {
    // Just do a normal traversal if we're not currently rewriting in a CFG.
    return e->traverse(*this->self());
  }

  // End current block with a branch
  SExpr* nc = this->self()->traverseArg(e->condition());
  Branch* br = newBranch(nc);

  // If the current continuation is null, then make a new one.
  BasicBlock* currCont = currentContinuation();
  BasicBlock* cont = currCont;
  if (!cont)
    cont = newBlock(1);

  // Process the then and else blocks
  beginBlock(br->thenBlock());
  setContinuation(cont);
  this->self()->traverse(e->thenExpr(), TRV_Tail);

  beginBlock(br->elseBlock());
  setContinuation(cont);
  this->self()->traverse(e->elseExpr(), TRV_Tail);
  setContinuation(currCont);    // restore original continuation

  // If we had an existing continuation, then we're done.
  // The then/else blocks will call the continuation.
  if (currCont)
    return nullptr;

  // Otherwise, if we created a new continuation, then start processing it.
  beginBlock(cont);
  assert(cont->arguments().size() > 0);
  return cont->arguments()[0];
}


void CFGReducer::traversePendingBlocks() {
  // Save the current context.
  std::unique_ptr<VarContext> oldVarCtx = std::move(varCtx_);

  // Process pending blocks.
  while (!pendingBlockQueue_.empty()) {
    unsigned pi = pendingBlockQueue_.front();
    pendingBlockQueue_.pop();

    PendingBlock* pb = &pendingBlocks_[pi];
    if (!pb->continuation || pb->processed)
      continue;   // unreachable or already processed block.

    // std::cerr << "processing pending block " << pi << "\n";
    // TILDebugPrinter::print(pb->expr, std::cerr);
    // std::cerr << "\n";

    varCtx_ = std::move(pb->ctx);
    setContinuation(pb->continuation);
    beginBlock(pb->block);
    SExpr *e = pb->expr;

    traverse(e, TRV_Tail);  // may invalidate pb

    setContinuation(nullptr);
    varCtx_ = nullptr;

    // traversal may have invalidated pb
    pendingBlocks_[pi].processed = true;  // mark block as processed.
  }

  // Restore the current context.
  varCtx_ = std::move(oldVarCtx);
}



SCFG* CFGReducer::beginSCFG(SCFG *Cfg, unsigned NBlocks, unsigned NInstrs) {
  CopyReducer::beginSCFG(Cfg, NBlocks, NInstrs);
  beginBlock(currentCFG()->entry());
  setContinuation(currentCFG()->exit());
  return currentCFG();
}


void CFGReducer::endSCFG() {
  setContinuation(nullptr);
  traversePendingBlocks();
  currentCFG()->computeNormalForm();
  SCFG* Scfg = currentCFG();
  CopyReducer::endSCFG();

  std::cerr << "\n===== Lowered ======\n";
  TILDebugPrinter::print(Scfg, std::cerr);

  SSAPass::ssaTransform(Scfg, Arena);
  std::cerr << "\n===== SSA ======\n";
  TILDebugPrinter::print(Scfg, std::cerr);

  SExpr *ncfg = CFGCopier::copy(Scfg, Arena);
  std::cerr << "\n===== Copy ======\n";
  TILDebugPrinter::print(ncfg, std::cerr);
}


SExpr* CFGReducer::lower(SExpr *e, MemRegionRef a) {
  CFGReducer traverser(a);
  return traverser.traverse(e, TRV_Tail);
}


}  // end namespace ohmu
