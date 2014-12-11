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
    auto* S = Reducer->switchScope(Scope);
    Reducer->beginCFG(nullptr);
    Reducer->traverse(PendingExpr, TRV_Tail);
    auto *res = Reducer->currentCFG();
    Reducer->endCFG();
    Reducer->restoreScope(S);

    finish();
    return res;
  }
};



/// Set the BaseType of i, based on the type expression e.
static void setBaseTypeFromExpr(Instruction* i, SExpr* e) {
  if (!e)
    return;
  if (auto *f = dyn_cast<Future>(e))
    e = f->force();

  switch (e->opcode()) {
    case COP_Function:
    case COP_Code:
    case COP_Field:
    case COP_Record:
      i->setBaseType(BaseType::getBaseType<void*>());
      break;
    case COP_ScalarType:
      i->setBaseType(cast<ScalarType>(e)->baseType());
      break;
    case COP_Literal:
      i->setBaseType(cast<Literal>(e)->baseType());
      break;
    default:
      assert(false && "Type expression must be a value.");
      break;
  }
}


// Traverse e to find its bounding type, and set the value type of res.
// Returns res; the bounding type is stored in resultType_.
SExpr* CFGReducer::calculateResidualType(SExpr* res, SExpr* e) {
  // Short-circuit: no need for detailed type info if res is not a pointer.
  // I.e. don't traverse arithmetic expressions.
  auto* ires = dyn_cast_or_null<Instruction>(res);
  if (ires && ires->baseType().Base != BaseType::BT_Pointer
           && ires->baseType().Base != BaseType::BT_Void) {
    return res;
  }

  // Short-circuit: the bounding type of a value is itself.
  // We don't want to copy the value!
  if (e->isValue()) {
    resultType_.set(e, BoundingType::BT_Equivalent);
  }
  else {
    // auto m = switchMode(RM_Promote);
    auto b = switchEmit(false);

    // Type will be stored in resultType_.  We discard the result.
    traverse(e, TRV_Path);

    restoreEmit(b);
    // restoreMode(m);
  }

  // Use the type expression we computed to set the valueType.
  if (ires)
    setBaseTypeFromExpr(ires, resultType_.typeExpr());

  return res;
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
      SExpr* res = entry.Subst;
      SExpr* e   = res;

      // Promote variables.  (See reduceVariable.)
      if (auto* v = dyn_cast<Variable>(res)) {
        e = v->variableDecl()->definition();

        // TODO: this is a hack to eliminate letrecs.  Fix it.
        if (evd->kind() == VarDecl::VK_Letrec)
          res = nullptr;
      }

      // TODO: this is a hack to eliminate let for heap values.  Fix it.
      if (evd->kind() == VarDecl::VK_Let && res->isHeapValue())
        res = nullptr;

      // A null scope means that we are rewriting in the output scope.
      // (See reduceVariable.)
      auto* s = switchScope(nullptr);
      calculateResidualType(res, e);   // stores type of e in in resultType_.
      restoreScope(s);
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

      auto* sdef = slt->definition();
      if (slt->hasModifier(Slot::SLT_Final) && sdef->isTrivial()) {
        // TODO: do we want to be a bit more sophisticated here?
        resultType_.set(sdef, BoundingType::BT_Equivalent);
        return sdef;
      }

      auto* sapp = newApply(svar, nullptr, Apply::FAK_SApply);
      auto* res  = newProject(sapp, idstr);

      resultArgs_.push_back(svar);
      resultType_.set(sdef, BoundingType::BT_Type);
      // TODO:  automatically insert loads for slots.
      setBaseTypeFromExpr(res, sdef);
      return res;
    }
  }

  diag.error("Identifier not found: ") << idstr;
  resultType_.clear();
  return new (Arena) Identifier(orig);
}



Function* CFGReducer::reduceFunction(Function &orig, VarDecl *nvd, SExpr* e0) {
  auto* res = CopyReducer::reduceFunction(orig, nvd, e0);
  resultType_.set(res, BoundingType::BT_Equivalent);
  return res;
}


Code* CFGReducer::reduceCode(Code &orig, SExpr* e0, SExpr* e1) {
  auto* res = CopyReducer::reduceCode(orig, e0, e1);
  resultType_.set(res, BoundingType::BT_Equivalent);
  return res;
}


Field* CFGReducer::reduceField(Field &orig, SExpr* e0, SExpr* e1) {
  auto* res = CopyReducer::reduceField(orig, e0, e1);
  resultType_.set(res, BoundingType::BT_Equivalent);
  return res;
}


Record* CFGReducer::reduceRecordEnd(Record *res) {
  resultType_.set(res, BoundingType::BT_Equivalent);
  return res;
}



SExpr* CFGReducer::reduceVariable(Variable &orig, VarDecl *vd) {
  SExpr* res;
  SExpr* e;

  if (Scope) {
    // Look up the substitution for this variable, which will be the residual.
    // The substitution is an expression in the output scope.
    res = Scope->lookupVar(orig.variableDecl());

    // The default substitution just rewrites a variable to a new variable,
    // so optimize for that case.
    if (auto* v = dyn_cast<Variable>(res))
      e = v->variableDecl()->definition();
    else
      e = res;
  }
  else {
    // If Scope is null, then we are rewriting an expression that is in the
    // output scope.  Don't substitute, just promote the variable.
    res = &orig;
    e   = orig.variableDecl()->definition();
  }

  // Set scope to null, which signifies the output scope.
  auto* s = switchScope(nullptr);
  calculateResidualType(res, e);   // stores type of e in in resultType_.
  restoreScope(s);

  // Return the substitution as a residual.
  return res;
}



SExpr* CFGReducer::reduceApply(Apply &orig, SExpr* e, SExpr *a) {
  // resultType_ holds the type of e.
  auto* f = dyn_cast_or_null<Function>(resultType_.typeExpr());

  if (!f) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a function: ") << e;
    resultType_.clear();
    return newUndefined();
  }

  // Handle self-arguments.
  if (!a && orig.applyKind() == Apply::FAK_SApply)
    a = e;

  // Set the result type, and the result arguments.
  auto* restyp = f->body();       // TODO -- evaluate body
  resultArgs_.push_back(a);
  resultType_.set(restyp);

  if (e && mode_ == RM_Reduce) {
    auto* res = CopyReducer::reduceApply(orig, e, a);
    setBaseTypeFromExpr(res, restyp);
    return res;
  }
  return nullptr;
}



SExpr* CFGReducer::reduceProject(Project &orig, SExpr* e) {
  // resultType_ holds the type of e.
  auto* r = dyn_cast_or_null<Record>(resultType_.typeExpr());
  if (!r) {
    // syntactic sugar: automatically insert self-applications if necessary.
    auto* sfuntyp = dyn_cast_or_null<Function>(resultType_.typeExpr());
    if (sfuntyp && sfuntyp->isSelfApplicable()) {
      resultArgs_.push_back(e);                   // push self-argument.
      r = dyn_cast<Record>(sfuntyp->body());
      e = newApply(e, nullptr, Apply::FAK_SApply);
    }
  }

  if (!r) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(e))
      diag.error("Expression is not a record: ") << e;
    resultType_.clear();
    return newUndefined();
  }

  Slot* slt = r->findSlot(orig.slotName());
  if (!slt) {
    diag.error("Slot not found: ") << orig.slotName();
    resultType_.clear();
    return newUndefined();
  }

  // Set the result type
  auto* restyp = slt->definition();   // TODO -- evaluate definition.
  resultType_.set(restyp);

  if (e && mode_ == RM_Reduce) {
    auto* res = CopyReducer::reduceProject(orig, e);
    setBaseTypeFromExpr(res, restyp);
    return res;
  }
  return nullptr;
}



SExpr* CFGReducer::reduceCall(Call &orig, SExpr *e) {
  // Apply pushes arguments onto resultArgs_.
  // Call will consume those arguments.
  auto* c = dyn_cast_or_null<Code>(resultType_.typeExpr());
  if (c) {
    if (!e) {
      auto it = codeMap_.find(c);
      if (it != codeMap_.end())
        return inlineLocalCall(it->second, c);   // calls clearPendingArgs().
    }
  }
  else {
    if (!isa<Undefined>(e))
      diag.error("Expression is not a code block: ") << e;
    resultArgs_.clear();
    resultType_.clear();
    return newUndefined();
  }

  resultArgs_.clear();
  auto* restyp = c->returnType();     // TODO -- evaluate return type.
  resultType_.set(restyp);

  if (e && mode_ == RM_Reduce) {
    auto* res = CopyReducer::reduceCall(orig, e);
    setBaseTypeFromExpr(res, restyp);
    return res;
  }
  return nullptr;
}



/// Convert a call expression to a goto for locally-defined functions.
/// Locally-defined functions map to a basic blocks.
SExpr* CFGReducer::inlineLocalCall(PendingBlock *pb, Code* c) {
  // All calls are tail calls.  Make a continuation if we don't have one.
  BasicBlock* cont = currentContinuation();
  if (!cont)
    cont = newBlock(1);
  // TODO: should we check against an existing type?
  // TODO: evaluate returnType()
  setBaseTypeFromExpr(cont->arguments()[0], c->returnType());

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
  newGoto(pb->block, resultArgs_.elements());

  resultArgs_.clear();
  resultType_.clear();

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
      res->setBaseType(instr->baseType());
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
      if (!i0->baseType().isNumeric())
        diag.error("Operator requires a numeric type: ") << &orig;
      break;
    }
    case UOP_BitNot: {
      if (i0->baseType().Base != BaseType::BT_Int)
        diag.error("Bitwise operations require integer type.") << &orig;
      break;
    }
    case UOP_LogicNot: {
      if (i0->baseType().Base != BaseType::BT_Bool)
        diag.error("Logical operations require boolean type.") << &orig;
      break;
    }
  }

  auto* res = CopyReducer::reduceUnaryOp(orig, i0);
  res->setBaseType(i0->baseType());
  return res;
}



bool CFGReducer::checkAndExtendTypes(Instruction*& i0, Instruction*& i1) {
  if (i0->baseType() == i1->baseType())
    return true;
  TIL_CastOpcode op = typeConvertable(i0->baseType(), i1->baseType());
  if (op != CAST_none) {
    i0 = newCast(op, i0);
    i0->setBaseType(i1->baseType());
    return true;
  }
  op = typeConvertable(i1->baseType(), i0->baseType());
  if (op != CAST_none) {
    i1 = newCast(op, i1);
    i1->setBaseType(i0->baseType());
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

  BaseType vt = BaseType::getBaseType<void>();
  switch (orig.binaryOpcode()) {
    case BOP_Add:
    case BOP_Sub:
    case BOP_Mul:
    case BOP_Div:
    case BOP_Rem: {
      if (!i0->baseType().isNumeric())
        diag.error("Operator requires a numeric type: ") << &orig;
      vt = i0->baseType();
      break;
    }
    case BOP_Shl:
    case BOP_Shr:
    case BOP_BitAnd:
    case BOP_BitXor:
    case BOP_BitOr: {
      if (i0->baseType().Base != BaseType::BT_Int)
        diag.error("Bitwise operations require integer type.") << &orig;
      vt = i0->baseType();
      break;
    }
    case BOP_Eq:
    case BOP_Neq:
    case BOP_Lt:
    case BOP_Leq: {
      vt = BaseType::getBaseType<bool>();
      break;
    }
    case BOP_Gt: {
      // rewrite > to <
      auto* res = newBinaryOp(BOP_Lt, i1, i0);
      res->setBaseType(BaseType::getBaseType<bool>());
      return res;
    }
    case BOP_Geq: {
      // rewrite >= to <=
      auto* res = newBinaryOp(BOP_Leq, i1, i0);
      res->setBaseType(BaseType::getBaseType<bool>());
      return res;
    }
    case BOP_LogicAnd:
    case BOP_LogicOr: {
      if (i0->baseType().Base != BaseType::BT_Bool)
        diag.error("Logical operations require boolean type.") << &orig;
      vt = BaseType::getBaseType<bool>();
      break;
    }
  }
  auto* res = CopyReducer::reduceBinaryOp(orig, i0, i1);
  res->setBaseType(vt);
  return res;
}



SExpr* CFGReducer::traverseCode(Code* e, TraversalKind k) {
  assert(Scope && "Cannot rewrite in output scope.");

  auto* nt = self()->traverse(e->returnType(), TRV_Type);

  // If we're not in a CFG, then evaluate body in a Future that creates one.
  // Otherwise set the body to null; it will be handled as a pending block.
  if (!currentCFG()) {
    auto* nb = new (Arena) CFGFuture(e->body(), this, Scope->clone());
    FutureQueue.push(nb);
    return self()->reduceCode(*e, nt, nb);
  }

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
    setBaseTypeFromExpr(ph, nvd->definition());

    // Make the function parameters look like let-variables.
    entry.VDecl = newVarDecl(VarDecl::VK_Let, entry.VDecl->varName(), ph);
    entry.Subst = ph;
  }

  // Add pb to the array of pending blocks.
  // It will not be enqueued until we see a call to the block.
  auto* pb = new PendingBlock(e->body(), b, ns);
  pendingBlocks_.emplace_back(std::unique_ptr<PendingBlock>(pb));

  // Create a code expr, and add it to the code map.
  Code* c = self()->reduceCode(*e, nt, nullptr);
  codeMap_.insert(std::make_pair(c, pb));
  return c;
}



SExpr* CFGReducer::traverseLet(Let* e, TraversalKind k) {
  assert(Scope && "Cannot rewrite in output scope.");

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
    // Don't allocVarIndex(), because we are eliminating let variable.
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
  if (!nci || nci->baseType().Base != BaseType::BT_Bool)
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
  // Process pending blocks.
  while (!pendingBlockQueue_.empty()) {
    PendingBlock* pb = pendingBlockQueue_.front();
    pendingBlockQueue_.pop();

    if (!pb->continuation)
      continue;   // unreachable block.

    auto *s = switchScope(pb->scope);
    setContinuation(pb->continuation);
    beginBlock(pb->block);

    traverse(pb->expr, TRV_Tail);  // may invalidate pb

    setContinuation(nullptr);
    restoreScope(s);
  }

  // Delete all pending blocks.
  // We wait until all blocks have been processed before deleting them.
  pendingBlocks_.clear();
  codeMap_.shrink_and_clear();
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


}  // end namespace til
}  // end namespace ohmu
