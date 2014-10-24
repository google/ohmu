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


/// A Future which creates a new CFG from the traversal.
class CFGFuture : public LazyCopyFuture<CFGReducer> {
public:
  CFGFuture(SExpr* e, CFGReducer* r, ScopeFrame* s)
      : LazyCopyFuture(e, r, s)
  { }

  virtual SExpr* traversePending() override {
    Reducer->beginCFG(nullptr);
    Reducer->Scope = std::move(this->Scope);
    Reducer->traverse(PendingExpr, TRV_Tail);
    auto *res = Reducer->currentCFG();
    Reducer->endCFG();
    return res;
  }
};


SExpr* CFGReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  if (auto* f = dyn_cast<Function>(e)) {
    pendingPathArgs_.push_back(a);
    return f->body();
  }
  assert(numPendingArgs() == 0 && "Internal error.");
  return CopyReducer::reduceApply(orig, e, a);
}


SExpr* CFGReducer::reduceProject(Project &orig, SExpr* e) {
  if (auto *r = dyn_cast<Record>(e)) {
    if (Slot* slt = r->findSlot(orig.slotName())) {
      return slt->definition();
    }
    else {
      std::cerr << "Slot not found: " << orig.slotName() << "\n";
      return new (Arena) Undefined();
    }
  }
  return CopyReducer::reduceProject(orig, e);
}


SExpr* CFGReducer::reduceCall(Call &orig, SExpr *e) {
  // Traversing Apply and SApply will push arguments onto pendingPathArgs_.
  // A call expression will consume the args.
  if (auto* c = dyn_cast<Code>(e)) {
    // TODO: handle more than one arg.
    auto it = codeMap_.find(c);
    if (it != codeMap_.end()) {
      // This is a locally-defined function, which maps to a basic block.
      PendingBlock* pb = it->second;

      // All calls are tail calls.  Make a continuation if we don't have one.
      BasicBlock* cont = currentContinuation();
      if (!cont)
        cont = newBlock(1);

      // Set the continuation of the pending block to the current continuation.
      // If there are multiple calls, the continuations must match.
      if (pb->continuation) {
        assert(pb->continuation == cont && "Cannot transform to tail call!");
      }
      else {
        pb->continuation = cont;
        // Once we have a continuation, we can add pb to the queue.
        pendingBlockQueue_.push(pb);
      }

      // End current block with a jump to the new one.
      newGoto(pb->block, pendingArgs());
      clearPendingArgs();

      // If this was a newly-created continuation, then continue where we
      // left off.
      if (!currentContinuation()) {
        beginBlock(cont);
        return cont->arguments()[0];
      }
      return nullptr;
    }
  }

  // Create Apply exprs for all pending arguments, and return a Call expr.
  SExpr *f = e;
  for (auto *a : pendingArgs())
    f = new (Arena) Apply(f, a);
  clearPendingArgs();

  return CopyReducer::reduceCall(orig, f);
}



SExpr* CFGReducer::traverseCode(Code* e, TraversalKind k) {
  auto* nt = self()->traverse(e->returnType(), TRV_Type);
  // If we're not in a CFG, then evaluate body in a Future that creates one.
  // Otherwise set the body to null; it will be handled as a pending block.
  if (!currentCFG()) {
    auto* nb = new (Arena) CFGFuture(e->body(), this, Scope->clone());
    FutureQueue.push(nb);
    return self()->reduceCode(*e, nt, nb);
  }
  return self()->reduceCode(*e, nt, nullptr);
}



SExpr* CFGReducer::reduceCode(Code& orig, SExpr* e0, SExpr* e1) {
  if (!currentCFG())
    return CopyReducer::reduceCode(orig, e0, e1);

  // Code blocks inside a CFG will be lowered to basic blocks.
  // Function arguments will become phi nodes in the block.
  unsigned nargs = 0;
  unsigned sz = scope().numVars();
  while (nargs < sz) {
    VarDecl* vd = scope().varDecl(nargs);
    if (vd && vd->kind() == VarDecl::VK_Fun)
      ++nargs;
    else
      break;
  }

  // TODO: right now, we assume that all local functions will become blocks.
  // Eventually, we'll need to handle proper nested lambdas.

  // Create a new block.
  BasicBlock *b = newBlock(nargs);
  // Clone the current context, but replace function parameters with
  // let-variables that refer to Phi nodes in the new block.
  ScopeFrame* ns = scope().clone();
  for (unsigned i = 0; i < nargs; ++i) {
    unsigned j   = nargs-1-i;
    StringRef nm = scope().varDecl(j)->name();
    b->arguments()[i]->setInstrName(nm);
    ns->setVar(j, new (Arena) VarDecl(nm, b->arguments()[i]));
  }

  // Add pb to the array of pending blocks.
  // It will not be enqueued until we see a call to the block.
  auto* pb = new PendingBlock(orig.body(), b, ns);
  pendingBlocks_.emplace_back(std::unique_ptr<PendingBlock>(pb));

  // Create a code expr, and add it to the code map.
  Code* c = CopyReducer::reduceCode(orig, e0, nullptr);
  codeMap_.insert(std::make_pair(c, pb));
  return c;
}



SExpr* CFGReducer::reduceIdentifier(Identifier &orig) {
  StringRef s = orig.name();

  // Search backward through the context until we find a match.
  for (unsigned i=0,n=scope().numVars(); i < n; ++i) {
    VarDecl* vd = scope().varDecl(i);
    if (!vd)
      continue;

    if (vd->name() == s) {
      // Translate identifier to a named variable.
      if (vd->kind() == VarDecl::VK_Let ||
          vd->kind() == VarDecl::VK_Letrec)
        // Map let variables directly to their definitions.
        return vd->definition();
      return new (Arena) Variable(vd);
    }
    else if (vd->kind() == VarDecl::VK_SFun) {
      if (!vd->definition())
        continue;

      // Map identifiers to slots for record self-variables.
      auto* sfun = cast<SFunction>(vd->definition());
      if (Record *r = dyn_cast<Record>(sfun->body())) {
        if (r->findSlot(s)) {
          auto* svar = new (Arena) Variable(vd);
          auto* sapp = new (Arena) SApply(svar);
          return new (Arena) Project(sapp, s);
        }
      }
    }
  }

  // TODO: emit warning on name-not-found.
  std::cerr << "error: Identifier " << s << " not found.\n";
  return new (Arena) Identifier(orig);
}



SExpr* CFGReducer::reduceLet(Let &orig, VarDecl *nvd, SExpr *b) {
  if (currentCFG())
    return b;   // eliminate the let
  else
    return CopyReducer::reduceLet(orig, nvd, b);
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
  std::unique_ptr<ScopeFrame> oldScope = std::move(Scope);

  // Process pending blocks.
  while (!pendingBlockQueue_.empty()) {
    PendingBlock* pb = pendingBlockQueue_.front();
    pendingBlockQueue_.pop();

    if (!pb->continuation)
      continue;   // unreachable block.

    // std::cerr << "processing pending block " << pi << "\n";
    // TILDebugPrinter::print(pb->expr, std::cerr);
    // std::cerr << "\n";

    Scope = std::move(pb->scope);
    setContinuation(pb->continuation);
    beginBlock(pb->block);
    SExpr *e = pb->expr;

    traverse(e, TRV_Tail);  // may invalidate pb

    setContinuation(nullptr);
    Scope = nullptr;
  }

  // Delete all pending blocks.
  // We wait until all blocks have been processed before deleting them.
  pendingBlocks_.clear();
  codeMap_.shrink_and_clear();

  // Restore the current context.
  Scope = std::move(oldScope);
}



SCFG* CFGReducer::beginCFG(SCFG *Cfg, unsigned NBlocks, unsigned NInstrs) {
  CopyReducer::beginCFG(Cfg, NBlocks, NInstrs);
  beginBlock(currentCFG()->entry());
  setContinuation(currentCFG()->exit());
  return currentCFG();
}


void CFGReducer::endCFG() {
  setContinuation(nullptr);
  traversePendingBlocks();

  // currentCFG()->renumber();
  // std::cerr << "\n===== Lowered ======\n";
  // TILDebugPrinter::print(currentCFG(), std::cerr);

  currentCFG()->computeNormalForm();
  SCFG* Scfg = currentCFG();
  CopyReducer::endCFG();

  //std::cerr << "\n===== Normalized ======\n";
  //TILDebugPrinter::print(Scfg, std::cerr);

  SSAPass::ssaTransform(Scfg, Arena);
  //std::cerr << "\n===== SSA ======\n";
  //TILDebugPrinter::print(Scfg, std::cerr);

  //SExpr *ncfg = CFGCopier::copy(Scfg, Arena);
  //cast<SCFG>(ncfg)->computeNormalForm();
  //std::cerr << "\n===== Copy ======\n";
  //TILDebugPrinter::print(ncfg, std::cerr);
}


SExpr* CFGReducer::lower(SExpr *e, MemRegionRef a) {
  CFGReducer traverser(a);
  return traverser.traverseAll(e);
}


}  // end namespace ohmu
