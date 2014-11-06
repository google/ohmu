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

  virtual SExpr* evaluate() override {
    Reducer->beginCFG(nullptr);
    Reducer->Scope = std::move(this->Scope);
    Reducer->traverse(PendingExpr, TRV_Tail);
    auto *res = Reducer->currentCFG();
    Reducer->endCFG();
    PendingExpr = nullptr;
    return res;
  }
};



// Map identifiers to variable names, or to slot definitions.
SExpr* CFGReducer::reduceIdentifier(Identifier &orig) {
  StringRef idstr = orig.name();

  // Search backward through the context until we find a match.
  for (unsigned i=0,n=scope().numVars(); i < n; ++i) {
    VarDecl* vd = scope().varDecl(i);
    if (!vd)
      continue;

    if (vd->name() == idstr) {
      // Translate identifier to a named variable.
      if (vd->kind() == VarDecl::VK_Let ||
          vd->kind() == VarDecl::VK_Letrec) {
        // Map let variables directly to their definitions.
        return vd->definition();
      }
      // Construct a variable, and set it's type.
      auto* res = new (Arena) Variable(vd);
      res->setBoundingType(vd->definition(), BoundingType::BT_Type);
      return res;
    }
    else if (vd->kind() == VarDecl::VK_SFun) {
      // Map identifiers to slots for record self-variables.
      if (!vd->definition())
        continue;
      auto* sfun = cast<Function>(vd->definition());
      Record* rec = dyn_cast<Record>(sfun->body());
      if (!rec)
        continue;
      auto* slt = rec->findSlot(idstr);
      if (!slt)
        continue;

      if (slt->hasModifier(Slot::SLT_Final) &&
          slt->definition()->isTrivial()) {
        // TODO: do we want to be a bit more sophisticated here?
        return slt->definition();
      }
      auto* svar = new (Arena) Variable(vd);
      svar->setBoundingType(sfun, BoundingType::BT_Type);
      auto* sapp = new (Arena) Apply(svar, nullptr, Apply::FAK_SApply);
      sapp->setBoundingType(rec,  BoundingType::BT_Type);
      auto* res  = new (Arena) Project(sapp, idstr);
      res->setBoundingType(slt->definition(), BoundingType::BT_Type);
      return res;
    }
  }

  // TODO: emit warning on name-not-found.
  diag.error("Identifier not found: ") << idstr;
  return new (Arena) Identifier(orig);
}



SExpr* CFGReducer::reduceVariable(Variable &orig, VarDecl *vd) {
  auto* res = CopyReducer::reduceVariable(orig, vd);
  auto k  = vd->kind();
  auto bt = (k == VarDecl::VK_Let || k == VarDecl::VK_Letrec) ?
      BoundingType::BT_Equivalent : BoundingType::BT_Type;
  res->setBoundingType(vd->definition(), bt);
  return res;
}



SExpr* CFGReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  auto* f = dyn_cast<Function>(e);
  Instruction* fi = nullptr;
  bool isVal = f;

  if (!f) {
    fi = dyn_cast<Instruction>(e);
    if (fi) {
      auto* ftyp = fi->getBoundingTypeValue();
      f = dyn_cast_or_null<Function>(ftyp);
    }
  }

  if (!f) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a function: ") << e;
    return new (Arena) Undefined();
  }

  pendingPathArgs_.push_back(a);
  if (isVal) {
    // Partially evaluate the apply.
    return f->body();
  }

  // Construct a residual, and set its type.
  auto *res = CopyReducer::reduceApply(orig, e, a);
  res->setBoundingType(f->body(), fi->boundingType().Rel);
  return res;
}



SExpr* CFGReducer::reduceProject(Project &orig, SExpr* e) {
  auto* r = dyn_cast<Record>(e);
  Instruction* ri = nullptr;
  bool isVal = r;
  if (!r) {
    ri = dyn_cast<Instruction>(e);
    if (ri) {
      auto* rtyp = ri->getBoundingTypeValue();
      r = dyn_cast_or_null<Record>(rtyp);
      if (!r) {
        // Automatically insert implicit self-applications.
        auto* sfuntyp = dyn_cast<Function>(rtyp);
        if (sfuntyp && sfuntyp->isSelfApplicable()) {
          r = dyn_cast<Record>(sfuntyp->body());
          if (r) {
            ri = new (Arena) Apply(e, nullptr, Apply::FAK_SApply);
            ri->setBoundingType(r, ri->boundingType().Rel);
          }
        }
      }
    }
  }

  if (!r) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a record: ") << e;
    return new (Arena) Undefined();
  }

  Slot* slt = r->findSlot(orig.slotName());
  if (!slt) {
    diag.error("Slot not found: ") << orig.slotName();
    return new (Arena) Undefined();
  }

  if (isVal) {
    // Partially evaluate the project.
    return slt->definition();
  }
  // Construct a residual.
  auto* res = CopyReducer::reduceProject(orig, ri);
  res->setBoundingType(slt->definition(), ri->boundingType().Rel);
  return res;
}



SExpr* CFGReducer::reduceCall(Call &orig, SExpr *e) {
  // Reducing Apply pushes arguments onto pendingPathArgs_.
  // Reducing Call will consume those arguments.
  auto* c = dyn_cast<Code>(e);
  if (c) {
    auto it = codeMap_.find(c);
    if (it != codeMap_.end()) {
      // inlineLocalCall with clearPendingArgs()
      return inlineLocalCall(it->second, c);
    }
  }
  Instruction* ci = nullptr;
  // bool isVal = c;
  if (!c) {
    ci = dyn_cast<Instruction>(e);
    if (ci) {
      auto* ctyp = ci->getBoundingTypeValue();
      c = dyn_cast<Code>(ctyp);
    }
  }

  if (!c) {
    if (!isa<Undefined>(e))
      diag.error("Expression is not a code block: ") << e;
    clearPendingArgs();
    return new (Arena) Undefined();
  }

  clearPendingArgs();

  auto* res = CopyReducer::reduceCall(orig, e);
  res->setBoundingType(c->returnType(), BoundingType::BT_Type);
  return res;
}



/// Convert a call expression to a goto for locally-defined functions.
/// Locally-defined functions map to a basic blocks.
SExpr* CFGReducer::inlineLocalCall(PendingBlock *pb, Code* c) {
  // All calls are tail calls.  Make a continuation if we don't have one.
  BasicBlock* cont = currentContinuation();
  if (!cont)
    cont = newBlock(1);
  // TODO: should we check against an existing type?
  cont->arguments()[0]->setBoundingType(c->returnType(),
                                        BoundingType::BT_Type);

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



SExpr* CFGReducer::reduceLoad(Load &orig, SExpr* e) {
  auto* res = CopyReducer::reduceLoad(orig, e);
  // If we can map the load to a local variable, then set the type.
  if (Alloc* a = dyn_cast<Alloc>(e)) {
    if (auto* instr = dyn_cast_or_null<Instruction>(a->initializer()))
      res->setValueType(instr->valueType());
  }
  return res;
}



SExpr* CFGReducer::reduceUnaryOp(UnaryOp &orig, SExpr* e0) {
  Instruction* i0 = dyn_cast<Instruction>(e0);
  if (!i0) {
    diag.error("Invalid use of arithmetic operator: ") << &orig;
    return new (Arena) Undefined();
  }

  switch (orig.unaryOpcode()) {
    case UOP_Minus: {
      if (!i0->valueType().isNumeric())
        diag.error("Operator requires a numeric type: ") << &orig;
      break;
    }
    case UOP_BitNot: {
      if (i0->valueType().Base != ValueType::BT_Int)
        diag.error("Bitwise operations require integer type.") << &orig;
      break;
    }
    case UOP_LogicNot: {
      if (i0->valueType().Base != ValueType::BT_Bool)
        diag.error("Logical operations require boolean type.") << &orig;
      break;
    }
  }

  auto* res = CopyReducer::reduceUnaryOp(orig, i0);
  res->setValueType(i0->valueType());
  return res;
}



bool CFGReducer::checkAndExtendTypes(Instruction*& i0, Instruction*& i1) {
  if (i0->valueType() == i1->valueType())
    return true;
  TIL_CastOpcode op = typeConvertable(i0->valueType(), i1->valueType());
  if (op != CAST_none) {
    i0 = addInstr(new (Arena) Cast(op, i0));
    i0->setValueType(i1->valueType());
    return true;
  }
  op = typeConvertable(i1->valueType(), i0->valueType());
  if (op != CAST_none) {
    i1 = addInstr(new (Arena) Cast(op, i1));
    i1->setValueType(i0->valueType());
    return true;
  }
  return false;
}



SExpr* CFGReducer::reduceBinaryOp(BinaryOp &orig, SExpr* e0, SExpr* e1) {
  Instruction* i0 = dyn_cast<Instruction>(e0);
  Instruction* i1 = dyn_cast<Instruction>(e1);
  if (!i0 || !i1) {
    diag.error("Invalid use of arithmetic operator: ") << &orig;
    return new (Arena) Undefined();
  }

  if (!checkAndExtendTypes(i0, i1)) {
    diag.error("Arithmetic operation on incompatible types: ") << &orig;
    return new (Arena) Undefined();
  }

  ValueType vt = ValueType::getValueType<void>();
  switch (orig.binaryOpcode()) {
    case BOP_Add:
    case BOP_Sub:
    case BOP_Mul:
    case BOP_Div:
    case BOP_Rem: {
      if (!i0->valueType().isNumeric())
        diag.error("Operator requires a numeric type: ") << &orig;
      vt = i0->valueType();
      break;
    }
    case BOP_Shl:
    case BOP_Shr:
    case BOP_BitAnd:
    case BOP_BitXor:
    case BOP_BitOr: {
      if (i0->valueType().Base != ValueType::BT_Int)
        diag.error("Bitwise operations require integer type.") << &orig;
      vt = i0->valueType();
      break;
    }
    case BOP_Eq:
    case BOP_Neq:
    case BOP_Lt:
    case BOP_Leq: {
      vt = ValueType::getValueType<bool>();
      break;
    }
    case BOP_Gt: {
      // rewrite > to <
      auto* res = addInstr(new (Arena) BinaryOp(BOP_Lt, i1, i0));
      res->setValueType(ValueType::getValueType<bool>());
      return res;
    }
    case BOP_Geq: {
      // rewrite >= to <=
      auto* res = addInstr(new (Arena) BinaryOp(BOP_Leq, i1, i0));
      res->setValueType(ValueType::getValueType<bool>());
      return res;
    }
    case BOP_LogicAnd:
    case BOP_LogicOr: {
      if (i0->valueType().Base != ValueType::BT_Bool)
        diag.error("Logical operations require boolean type.") << &orig;
      vt = ValueType::getValueType<bool>();
      break;
    }
  }
  auto* res = CopyReducer::reduceBinaryOp(orig, i0, i1);
  res->setValueType(vt);
  return res;
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
    unsigned j  = nargs-1-i;
    VarDecl* vd = scope().varDecl(j);
    Phi*     ph = b->arguments()[i];
    ph->setInstrName(vd->name());
    ph->setBoundingType(vd->definition(), BoundingType::BT_Type);
    ns->setVar(j, new (Arena) VarDecl(VarDecl::VK_Let, vd->name(), ph));
  }

  // Add pb to the array of pending blocks.
  // It will not be enqueued until we see a call to the block.
  auto* pb = new PendingBlock(orig.body(), b, ns);
  pendingBlocks_.emplace_back(std::unique_ptr<PendingBlock>(pb));

  // Create a code expr, and add it to the code map.
  Code* c = CopyReducer::reduceCode(orig, e0, e1);
  codeMap_.insert(std::make_pair(c, pb));
  return c;
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
  Instruction* nci = dyn_cast<Instruction>(nc);
  if (!nci || nci->valueType().Base != ValueType::BT_Bool)
    diag.error("Branch condition is not a boolean: ") << nci;

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

  // std::cerr << "\n===== Normalized ======\n";
  // TILDebugPrinter::print(Scfg, std::cerr);

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
