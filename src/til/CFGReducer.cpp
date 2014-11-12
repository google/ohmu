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
#include "SSAPass.h"

namespace ohmu {
namespace til  {


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


// Set bounding type of residual.
void CFGReducer::setResidualBoundingType(Instruction* res, SExpr* typ,
                                         BoundingType::Relation rel) {
  if (Future* fut = dyn_cast<Future>(typ))
    typ = fut->force();

  if (typ->isValue()) {
    // Variable definition is a value, so use that as the type.
    res->setBoundingType(typ, rel);
    return;
  }
  auto* ityp = dyn_cast<Instruction>(typ);
  if (ityp) {
    auto* vtyp = ityp->boundingTypeExpr();
    if (vtyp) {
      assert(vtyp->isValue() && "Bounding type must be a value.");
      // Grab the upper bound of the type expr.
      auto vtrel = ityp->boundingType().Rel;
      res->setBoundingType(vtyp, BoundingType::minRelation(rel, vtrel));
      return;
    }
  }
  diag.error("Type does not have a value upper bound: ") << typ;
}



// Map identifiers to variable names, or to slot definitions.
SExpr* CFGReducer::reduceIdentifier(Identifier &orig) {
  StringRef idstr = orig.idString();

  // Search backward through the context until we find a match.
  for (unsigned i=0,n=scope().numVars(); i < n; ++i) {
    auto& entry = scope().entry(i);
    VarDecl *evd = entry.VDecl;

    // First check to see if the identifier refers to a named variable.
    if (evd->varName() == idstr) {
      // For lets, the substitution can be anything; see traverseLet.
      // Eliminate the let by returning the substitution.
      if (evd->kind() == VarDecl::VK_Let)
        return entry.Subst;

      // For letrecs, the substitution should be a variable.
      // Eliminate the letrec by returning the variable definition.
      if (evd->kind() == VarDecl::VK_Letrec) {
        auto* var = cast<Variable>(entry.Subst);
        return var->variableDecl()->definition();
      }

      // For function parameters, the substitution should be a variable.
      auto* res = cast<Variable>(entry.Subst);
      if (res->boundingTypeExpr() == nullptr)
        setResidualBoundingType(res, res->variableDecl()->definition(),
                                BoundingType::BT_Type);
      return res;
    }
    // Otherwise look for slots in enclosing modules
    else if (evd->kind() == VarDecl::VK_SFun) {
      auto* svar = cast<Variable>(entry.Subst);
      auto* svd  = svar->variableDecl();

      // Map identifiers to slots for record self-variables.
      if (!svd->definition())
        continue;
      auto* sfun = cast<Function>(svd->definition());
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
      auto* sapp = newApply(svar, nullptr, Apply::FAK_SApply);
      auto* res  = newProject(sapp, idstr);
      setResidualBoundingType(res, slt->definition(), BoundingType::BT_Type);
      return res;
    }
  }

  diag.error("Identifier not found: ") << idstr;
  return new (Arena) Identifier(orig);
}



SExpr* CFGReducer::reduceVariable(Variable &orig, VarDecl *vd) {
  auto* res = CopyReducer::reduceVariable(orig, vd);

  // TODO: this is ugly.  fix it.
  if (auto* var = dyn_cast<Variable>(res)) {
    auto k  = var->variableDecl()->kind();
    auto bt = (k == VarDecl::VK_Let || k == VarDecl::VK_Letrec) ?
      BoundingType::BT_Equivalent : BoundingType::BT_Type;
    if (var->boundingTypeExpr() == nullptr)
      setResidualBoundingType(var, var->variableDecl()->definition(), bt);
  }
  return res;
}



SExpr* CFGReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  auto* f = dyn_cast<Function>(e);
  Instruction* fi = nullptr;
  bool isVal = f;

  if (!f) {
    fi = dyn_cast<Instruction>(e);
    if (fi) {
      auto* ftyp = fi->boundingTypeExpr();
      f = dyn_cast_or_null<Function>(ftyp);
    }
  }

  if (!f) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a function: ") << e;
    return newUndefined();
  }

  pendingArgs_.push_back(a);
  if (isVal) {
    // Partially evaluate the Apply.
    return f->body();
  }

  // Construct a residual, and set its type.
  auto *res = CopyReducer::reduceApply(orig, e, a);
  setResidualBoundingType(res, f->body(), fi->boundingType().Rel);
  return res;
}



SExpr* CFGReducer::reduceProject(Project &orig, SExpr* e) {
  auto* r = dyn_cast<Record>(e);
  Instruction* ri = nullptr;
  bool isVal = r;
  if (!r) {
    ri = dyn_cast<Instruction>(e);
    if (ri) {
      auto* rtyp = ri->boundingTypeExpr();
      r = dyn_cast_or_null<Record>(rtyp);
      if (!r) {
        // Automatically insert implicit self-applications.
        auto* sfuntyp = dyn_cast_or_null<Function>(rtyp);
        if (sfuntyp && sfuntyp->isSelfApplicable()) {
          r = dyn_cast<Record>(sfuntyp->body());
          if (r) {
            auto *sapp = newApply(e, nullptr, Apply::FAK_SApply);
            sapp->setBoundingType(r, ri->boundingType().Rel);
            ri = sapp;
          }
        }
      }
    }
  }

  if (!r) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a record: ") << e;
    return newUndefined();
  }

  Slot* slt = r->findSlot(orig.slotName());
  if (!slt) {
    diag.error("Slot not found: ") << orig.slotName();
    return newUndefined();
  }

  if (isVal) {
    // Partially evaluate the Project.
    return slt->definition();
  }
  // Construct a residual.
  auto* res = CopyReducer::reduceProject(orig, ri);
  setResidualBoundingType(res, slt->definition(), ri->boundingType().Rel);
  return res;
}



SExpr* CFGReducer::reduceCall(Call &orig, SExpr *e) {
  // Reducing Apply pushes arguments onto pendingArgs_.
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
      auto* ctyp = ci->boundingTypeExpr();
      c = dyn_cast<Code>(ctyp);
    }
  }

  if (!c) {
    if (!isa<Undefined>(e))
      diag.error("Expression is not a code block: ") << e;
    pendingArgs_.clear();
    return newUndefined();
  }

  pendingArgs_.clear();

  auto* res = CopyReducer::reduceCall(orig, e);
  setResidualBoundingType(res, c->returnType(), BoundingType::BT_Type);
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
  setResidualBoundingType(cont->arguments()[0],
                          c->returnType(), BoundingType::BT_Type);

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
  newGoto(pb->block, pendingArgs_.elements());
  pendingArgs_.clear();

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
    return newUndefined();
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
    i0 = newCast(op, i0);
    i0->setValueType(i1->valueType());
    return true;
  }
  op = typeConvertable(i1->valueType(), i0->valueType());
  if (op != CAST_none) {
    i1 = newCast(op, i1);
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
    return newUndefined();
  }

  if (!checkAndExtendTypes(i0, i1)) {
    diag.error("Arithmetic operation on incompatible types: ") << &orig;
    return newUndefined();
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
      auto* res = newBinaryOp(BOP_Lt, i1, i0);
      res->setValueType(ValueType::getValueType<bool>());
      return res;
    }
    case BOP_Geq: {
      // rewrite >= to <=
      auto* res = newBinaryOp(BOP_Leq, i1, i0);
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
    auto& entry = scope().entry(nargs);
    if (entry.VDecl && entry.VDecl->kind() == VarDecl::VK_Fun)
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
    auto& entry = ns->entry(j);
    VarDecl *nvd = cast<Variable>(entry.Subst)->variableDecl();

    Phi* ph = b->arguments()[i];
    ph->setInstrName(entry.VDecl->varName());
    setResidualBoundingType(ph, nvd->definition(), BoundingType::BT_Type);

    // Make the function parameters look like let-variables.
    entry.VDecl = newVarDecl(VarDecl::VK_Let, entry.VDecl->varName(), ph);
    entry.Subst = ph;
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



SExpr* CFGReducer::traverseLet(Let* e, TraversalKind k) {
  if (!currentCFG())
    return SuperTv::traverseLet(e, k);

  // Otherwise we eliminate the let.
  auto *vd = e->variableDecl();
  bool scoped = (vd->varIndex() > 0) || (vd->varName().length() > 0);

  auto *e1 = traverse(vd->definition(), TRV_Decl);

  if (scoped) {
    if (auto *inst = dyn_cast<Instruction>(e1))
      inst->setInstrName(vd->varName());
    // Eliminate let, by replacing all occurrences of the let variable.
    Scope->enterScope(vd, e1);
  }

  auto *e2 = traverse(e->body(), TRV_Tail);

  if (scoped)
    Scope->exitScope(vd);

  return e2;
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


}  // end namespace til
}  // end namespace ohmu
