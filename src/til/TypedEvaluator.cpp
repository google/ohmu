//===- TypedEvaluator.cpp --------------------------------------*- C++ --*-===//
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


#include "TypedEvaluator.h"


namespace ohmu {
namespace til  {


class CFGFuture : public LazyCopyFuture<TypedEvaluator, ScopeCPS> {
public:
  typedef LazyCopyFuture<TypedEvaluator, ScopeCPS> Super;

  CFGFuture(SExpr* E, TypedEvaluator* R, ScopeCPS* S)
    : LazyCopyFuture(E, R, S)
  { }

  /// Create new CFG, and write results to the CFG.
  virtual SExpr* evaluate() override {
    CFGBuilder &B = Reducer->Builder;
    auto *Cfg = B.beginCFG(nullptr);
    B.beginBlock(Cfg->entry());
    ScopePtr->setCurrentContinuation(Cfg->exit());

    Super::evaluate();
    // TODO: should this be moved to Reducer->endCFG or some such?
    Reducer->processPendingBlocks();

    B.endCFG();
    Cfg->renumber();
    return Cfg;
  }
};


static TypedCopyAttr::Relation
getRelationFromVarDecl(VarDecl::VariableKind K) {
  switch (K) {
    case VarDecl::VK_Fun:  return TypedCopyAttr::BT_Type;
    case VarDecl::VK_SFun: return TypedCopyAttr::BT_ExactType;
    case VarDecl::VK_Let:  return TypedCopyAttr::BT_Equivalent;
  }
  return TypedCopyAttr::BT_Type;
}


/// Set the BaseType of i, based on the type expression e.
static void setBaseTypeFromExpr(Instruction* I, SExpr* Typ) {
  if (!Typ)
    return;

  if (auto *F = dyn_cast<Future>(Typ))
    Typ = F->force();

  switch (Typ->opcode()) {
    case COP_Function:
    case COP_Code:
    case COP_Field:
    case COP_Record:
      I->setBaseType(BaseType::getBaseType<void*>());
      break;
    case COP_ScalarType:
      I->setBaseType(cast<ScalarType>(Typ)->baseType());
      break;
    case COP_Literal:
      I->setBaseType(cast<Literal>(Typ)->baseType());
      break;
    default:
      assert(false && "Type expression must be a value.");
      break;
  }
}



void TypedEvaluator::reduceScalarType(ScalarType *Orig) {
  auto& Res = resultAttr();
  // Scalar types are globally defined; we share pointers.
  Res.Exp      = Orig;
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Orig;
}


void TypedEvaluator::reduceFunction(Function *Orig) {
  Super::reduceFunction(Orig);

  auto& Res    = resultAttr();
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Res.Exp;
  // TODO: need to set the substitution here.
}


void TypedEvaluator::reduceRecord(Record *Orig) {
  Super::reduceRecord(Orig);

  auto& Res    = resultAttr();
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Res.Exp;
  // TODO: need to set the substitution here.
}


void TypedEvaluator::reduceCode(Code *Orig) {
  Super::reduceCode(Orig);

  auto& Res    = resultAttr();
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Res.Exp;
  // TODO: need to set the substitution here.
}


void TypedEvaluator::reduceField(Field *Orig) {
  Super::reduceField(Orig);

  auto& Res    = resultAttr();
  Res.Rel      = TypedCopyAttr::BT_Equivalent;
  Res.TypeExpr = Res.Exp;
  // TODO: need to set the substitution here.
}


// If At.TypeExpr is an expression, then evaluate it.
void TypedEvaluator::evaluateTypeExpr(TypedCopyAttr &At) {
  if (At.TypeExpr->isValue())
    return;

  // Create a new scope from the substitution.
  ScopeCPS Ns;

  unsigned n = At.Subst.size();
  if (n > 0) {
    for (unsigned i = 0; i < n; ++i) {
      Ns.enterScope(nullptr, std::move(At.Subst[i]));
    }
    At.Subst.clear();
  }

  auto* S = switchScope(&Ns);
  computeAttrType(At, At.TypeExpr);
  restoreScope(S);
}


// Set the TypeExpr for At by evaluating E
void TypedEvaluator::computeAttrType(TypedCopyAttr &At, SExpr *E) {
  auto M = switchEvalMode(TEval_WeakHead);
  bool B = Builder.switchEmit(false);

  traverse(E, TRV_Decl);     // Type of E is stored in lastAttr()
  At.moveType(lastAttr());   // Copy type to original attribute
  popAttr();

  Builder.restoreEmit(B);
  restoreEvalMode(M);

  if (Instruction* I = dyn_cast_or_null<Instruction>(At.Exp))
    setBaseTypeFromExpr(I, At.TypeExpr);
}


// Promote the variable V, and store the result in resultAttr().
// Used by reduceVariable(), and reduceIdentifier().
void TypedEvaluator::promoteVariable(Variable *V) {
  auto& Res = resultAttr();

  Res.Exp = V;
  Res.Rel = getRelationFromVarDecl(V->variableDecl()->kind());

  // V is a variable in the output scope.
  // Thus, we need to create a new scope to evaluate the variable type,
  // with null substitutions for anything that V depends on.
  // TODO: optimize for this common case where the scope is empty.

  unsigned Vidx = V->variableDecl()->varIndex();
  ScopeCPS Ns;

  // We start at index 1, because 0 means undefined.
  for (unsigned i = 1; i < Vidx; ++i)
    Ns.enterScope(nullptr, TypedCopyAttr(nullptr));

  auto* S = switchScope(&Ns);
  computeAttrType(Res, V->variableDecl()->definition());
  restoreScope(S);
}



void TypedEvaluator::reduceVariable(Variable *Orig) {
  // We substitute for variables, so look up the substitution.
  auto& At = scope()->var( Orig->variableDecl()->varIndex() );
  if (At.TypeExpr) {
    // If we have a typed substitution, then return that.
    resultAttr() = At;
    return;
  }

  // If the substitution is for another variable, promote it.
  // Otherwise we have a null substitution, in which case we promote Orig.
  Variable *V;
  if (At.Exp)
    V = dyn_cast<Variable>(At.Exp);
  else
    V = Orig;

  assert(V && "Invalid substitution.");

  promoteVariable(V);
}



void TypedEvaluator::reduceApply(Apply *Orig) {
  auto& Res = resultAttr();
  auto& Fa  = attr(0);
  auto& Aa  = attr(1);

  auto* Fe  = Fa.Exp;
  auto* Ft  = Fa.TypeExpr;
  auto* F   = dyn_cast_or_null<Function>(Ft);

  if (!F) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(Fe))
      diag().error("Expression is not a function: ") << Fe;
    Res.Exp = Builder.newUndefined();
    return;
  }

  // Set the result type, substituting arguments for variables.
  Res.TypeExpr = F->body();     // TODO -- evaluate body
  Res.Rel = Fa.Rel;

  // Do lazy substitution
  if (!Aa.Exp && Orig->applyKind() == Apply::FAK_SApply) {
    // Handle implicit self-parameters
    TypedCopyAttr Facpy = Fa;            // copy Fa
    Res.stealSubstitution(Fa);           // move from Fa
    Res.pushSubst( std::move(Facpy) );
  }
  else {
    Res.stealSubstitution(Fa);
    Res.pushSubst( std::move(Aa) );
  }

  evaluateTypeExpr(Res);

  // Set the result residual.
  // TODO: we may not have a residual for Aa.Exp
  if (Fe) {
    auto* E = Builder.newApply(Fe, Aa.Exp, Orig->applyKind());
    setBaseTypeFromExpr(E, Res.TypeExpr);
    Res.Exp = E;
  }
}



void TypedEvaluator::reduceProject(Project *Orig) {
  auto& Res = resultAttr();
  auto& Ra  = attr(0);
  auto* Re  = Ra.Exp;
  auto* Rt  = Ra.TypeExpr;
  auto* R   = dyn_cast_or_null<Record>(Rt);

  /*
  if (!R) {
    // syntactic sugar: automatically insert self-applications if necessary.
    auto* Sf = dyn_cast_or_null<Function>(Rt);
    if (Sf && Sf->isSelfApplicable()) {
      resultArgs_.push_back(e);                   // push self-argument.
      r = dyn_cast<Record>(sfuntyp->body());
      e = newApply(e, nullptr, Apply::FAK_SApply);
    }
  }
  */

  if (!R) {
    // Undefined marks a previous error, so omit the warning.
    if (!isa<Undefined>(Re))
      diag().error("Expression is not a record: ") << Re;
    Res.Exp = Builder.newUndefined();
    return;
  }

  Slot* S = R->findSlot(Orig->slotName());
  if (!S) {
    diag().error("Slot not found: ") << Orig->slotName();
    Res.Exp = Builder.newUndefined();
    return;
  }

  // Set the result type.
  Res.TypeExpr = S->definition();
  Res.Rel      = Ra.Rel;
  Res.stealSubstitution(Ra);
  evaluateTypeExpr(Res);

  // Set the result residual.
  if (Re) {
    auto* E = Builder.newProject(Re, Orig->slotName());
    setBaseTypeFromExpr(E, Res.TypeExpr);
    Res.Exp = E;
  }
}



void TypedEvaluator::reduceCall(Call *Orig) {
  auto& Res = resultAttr();
  auto& Ca  = attr(0);
  auto* Ce  = Ca.Exp;
  auto* Ct  = Ca.TypeExpr;
  auto* C   = dyn_cast_or_null<Code>(Ct);

  if (!C) {
    if (!isa<Undefined>(Ce))
      diag().error("Expression is not a code block: ") << Ce;
    Res.Exp = Builder.newUndefined();
    return;
  }

  if (reduceNestedCall(Orig, C))
    return;

  // Set the result type.
  Res.TypeExpr = C->returnType();   // TODO -- evaluate definition.
  Res.Rel      = TypedCopyAttr::BT_Type;
  Res.stealSubstitution(Ca);
  evaluateTypeExpr(Res);

  // Set the result residual.
  if (Ce) {
    auto* E = Builder.newCall(Ce);
    setBaseTypeFromExpr(E, Res.TypeExpr);
    Res.Exp = E;
  }
}



void TypedEvaluator::reduceLoad(Load *Orig) {
  auto& Res = resultAttr();
  auto& Fa  = attr(0);
  auto* Fe  = Fa.Exp;
  auto* Ft  = Fa.TypeExpr;
  auto* F   = dyn_cast_or_null<Field>(Ft);

  if (!F) {
    if (!isa<Undefined>(Fe))
      diag().error("Expression is not a field: ") << Fe;
    Res.Exp = Builder.newUndefined();
    return;
  }

  Res.TypeExpr = F->range();  // TODO -- evaluate definition.
  Res.Rel      = TypedCopyAttr::BT_Type;
  Res.stealSubstitution(Fa);
  evaluateTypeExpr(Res);

  if (Fe) {
    auto* E = Builder.newLoad(Fe);
    setBaseTypeFromExpr(E, Res.TypeExpr);
    Res.Exp = E;
  }
}



void TypedEvaluator::reduceUnaryOp(UnaryOp *Orig) {
  auto& Res = resultAttr();
  Instruction* I0 = dyn_cast<Instruction>(attr(0).Exp);

  if (!I0) {
    diag().error("Invalid use of arithmetic operator: ") << Orig;
    Res.Exp = Builder.newUndefined();
    return;
  }

  switch (Orig->unaryOpcode()) {
    case UOP_Minus: {
      if (!I0->baseType().isNumeric())
        diag().error("Operator requires a numeric type: ") << Orig;
      break;
    }
    case UOP_BitNot: {
      if (I0->baseType().Base != BaseType::BT_Int)
        diag().error("Bitwise operations require integer type.") << Orig;
      break;
    }
    case UOP_LogicNot: {
      if (I0->baseType().Base != BaseType::BT_Bool)
        diag().error("Logical operations require boolean type.") << Orig;
      break;
    }
  }

  auto *Re = Builder.newUnaryOp(Orig->unaryOpcode(), I0);
  Re->setBaseType(I0->baseType());

  Res.Exp = Re;
  Res.Rel = TypedCopyAttr::BT_Type;
  Res.TypeExpr = nullptr;
}



bool TypedEvaluator::checkAndExtendTypes(Instruction*& I0, Instruction*& I1) {
  if (I0->baseType() == I1->baseType())
    return true;

  TIL_CastOpcode Op = typeConvertable(I0->baseType(), I1->baseType());
  if (Op != CAST_none) {
    I0 = Builder.newCast(Op, I0);
    I0->setBaseType(I1->baseType());
    return true;
  }
  Op = typeConvertable(I1->baseType(), I0->baseType());
  if (Op != CAST_none) {
    I1 = Builder.newCast(Op, I1);
    I1->setBaseType(I0->baseType());
    return true;
  }

  return false;
}



void TypedEvaluator::reduceBinaryOp(BinaryOp *Orig) {
  auto& Res = resultAttr();
  Instruction* I0 = dyn_cast<Instruction>(attr(0).Exp);
  Instruction* I1 = dyn_cast<Instruction>(attr(1).Exp);

  if (!I0 || !I1) {
    diag().error("Invalid use of arithmetic operator: ") << Orig;
    Res.Exp = Builder.newUndefined();
    return;
  }

  if (!checkAndExtendTypes(I0, I1)) {
    diag().error("Arithmetic operation on incompatible types: ") << Orig;
  }

  Instruction* Re = nullptr;
  BaseType Vt = BaseType::getBaseType<void>();
  switch (Orig->binaryOpcode()) {
    case BOP_Add:
    case BOP_Sub:
    case BOP_Mul:
    case BOP_Div:
    case BOP_Rem: {
      if (!I0->baseType().isNumeric())
        diag().error("Operator requires a numeric type: ") << Orig;
      Vt = I0->baseType();
      break;
    }
    case BOP_Shl:
    case BOP_Shr:
    case BOP_BitAnd:
    case BOP_BitXor:
    case BOP_BitOr: {
      if (I0->baseType().Base != BaseType::BT_Int)
        diag().error("Bitwise operations require integer type.") << Orig;
      Vt = I0->baseType();
      break;
    }
    case BOP_Eq:
    case BOP_Neq:
    case BOP_Lt:
    case BOP_Leq: {
      Vt = BaseType::getBaseType<bool>();
      break;
    }
    case BOP_Gt: {
      // rewrite > to <
      Re = Builder.newBinaryOp(BOP_Lt, I1, I0);
      Vt = BaseType::getBaseType<bool>();
      break;
    }
    case BOP_Geq: {
      // rewrite >= to <=
      Re = Builder.newBinaryOp(BOP_Leq, I1, I0);
      Vt = BaseType::getBaseType<bool>();
      break;
    }
    case BOP_LogicAnd:
    case BOP_LogicOr: {
      if (I0->baseType().Base != BaseType::BT_Bool)
        diag().error("Logical operations require boolean type.") << Orig;
      Vt = BaseType::getBaseType<bool>();
      break;
    }
  }
  if (!Re)
    Re = Builder.newBinaryOp(Orig->binaryOpcode(), I0, I1);
  Re->setBaseType(Vt);

  Res.Exp = Re;
  Res.Rel = TypedCopyAttr::BT_Type;
  Res.TypeExpr = nullptr;
}



void TypedEvaluator::reduceIdentifier(Identifier *Orig) {
  auto& Res = resultAttr();

  StringRef Idstr = Orig->idString();

  for (unsigned i = scope()->numVars()-1; i > 0; --i) {
    auto& Entry = scope()->entry(i);
    VarDecl *Vd = Entry.VDecl;
    if (!Vd)
      continue;

    // First check to see if the identifier refers to a named variable.
    if (Vd->varName() == Idstr) {
      if (Entry.VarAttr.TypeExpr) {
        // If we have a substitution with a type, return that.
        Res = Entry.VarAttr;
        return;
      }
      if (Variable *V = dyn_cast_or_null<Variable>(Entry.VarAttr.Exp)) {
        promoteVariable(V);
        return;
      }
      assert(false && "Invalid substitution.");
      return;
    }
    // Otherwise look up slot names in enclosing records.
    else if (Vd->kind() == VarDecl::VK_SFun) {
      // Map identifiers to slots for record self-variables.

      auto* Sv = dyn_cast_or_null<Variable>(Entry.VarAttr.Exp);
      if (!Sv)
        continue;
      auto* Svd = Sv->variableDecl();

      if (!Svd->definition())
        continue;

      auto* Sfun = cast<Function>(Svd->definition());
      auto* Rec  = dyn_cast<Record>(Sfun->body());
      if (!Rec)
        continue;
      auto* Slt = Rec->findSlot(Idstr);
      if (!Slt)
        continue;

      auto* Sdef = Slt->definition();
      if (Slt->hasModifier(Slot::SLT_Final) && Sdef->isTrivial()) {
        // Simply return the trivial value (i.e. call reduceTrivial())
        // TODO: this is a hack.
        Res.Exp      = Sdef;
        Res.Rel      = TypedCopyAttr::BT_Equivalent;
        Res.TypeExpr = Sdef;
        return;
      }

      auto* Eapp  = Builder.newApply(Sv, nullptr, Apply::FAK_SApply);
      Eapp->setBaseType(BaseType::getBaseType<void*>());

      auto* Eproj = Builder.newProject(Eapp, Idstr);
      setBaseTypeFromExpr(cast<Instruction>(Eproj), Sdef);

      // TODO -- evaluate definition
      Res.Exp      = Eproj;
      Res.Rel      = TypedCopyAttr::BT_Type;
      Res.TypeExpr = Sdef;

      unsigned Vidx =  Sv->variableDecl()->varIndex();
      assert(Vidx > 0 && "Variable index is not set.");

      // We start at index 1, because 0 means undefined.
      for (unsigned i = 1; i < Vidx; ++i)
        Res.pushSubst( TypedCopyAttr(nullptr) );
      Res.pushSubst( TypedCopyAttr(Sv) );
      return;
    }
  }

  diag().error("Identifier not found: ") << Idstr;
  Super::reduceIdentifier(Orig);
}



// Push substitutions for all variables from the current scope into At.
// If Vidx is specified, only includes vars up to Vidx.
void TypedEvaluator::pushScopeSubst(TypedCopyAttr& At, unsigned Vidx) {
  assert(At.Subst.empty() && "Substitution list must be empty.");

  if (Vidx == 0) Vidx = scope()->numVars();
  // We start at index 1, because index 0 means undefined.
  for (unsigned i = 1; i < Vidx; ++i)
    At.pushSubst(scope()->var(i));
}


void TypedEvaluator::traverseFunction(Function *Orig) {
  if (EvalMode == TEval_WeakHead) {
    auto& Res    = resultAttr();
    // We do not copy values, but instead construct a delayed substitution.
    // There is no valid residual, because the substitution hasn't been done.
    Res.Exp      = nullptr;
    Res.Rel      = TypedCopyAttr::BT_Equivalent;
    Res.TypeExpr = Orig;
    pushScopeSubst(Res);
    return;
  }
  SuperTv::traverseFunction(Orig);
}


void TypedEvaluator::traverseRecord(Record *Orig) {
  if (EvalMode == TEval_WeakHead) {
    // We do not copy values, but instead construct a delayed substitution.
    // There is no valid residual, because the substitution hasn't been done.
    auto& Res    = resultAttr();
    Res.Exp      = Orig;
    Res.Rel      = TypedCopyAttr::BT_Equivalent;
    Res.TypeExpr = Orig;
    pushScopeSubst(Res);
    return;
  }
  SuperTv::traverseRecord(Orig);
}


void TypedEvaluator::traverseCode(Code *Orig) {
  if (EvalMode == TEval_WeakHead) {
    // We do not copy values, but instead construct a delayed substitution.
    // There is no valid residual, because the substitution hasn't been done.
    auto& Res    = resultAttr();
    Res.Exp      = nullptr;
    Res.Rel      = TypedCopyAttr::BT_Equivalent;
    Res.TypeExpr = Orig;
    pushScopeSubst(Res);
    return;
  }

  if (Builder.currentBB()) {
    traverseNestedCode(Orig);
    return;
  }

  // Push the return type onto the stack.
  traverse(Orig->returnType(), TRV_Type);

  // We don't forward to Super here, because we have to use CFGFuture.
  // Make a new CFGFuture for the code body, and push it on the stack.
  auto *F = new (arena())
      CFGFuture(Orig->body(), this, scope()->clone());
  FutureQueue.push(F);
  auto *A = pushAttr();
  A->Exp = F;
  reduceCode(Orig);
}


void TypedEvaluator::traverseField(Field *Orig) {
  if (EvalMode == TEval_WeakHead) {
    // We do not copy values, but instead construct a delayed substitution.
    // There is no valid residual, because the substitution hasn't been done.
    auto& Res    = resultAttr();
    Res.Exp      = nullptr;
    Res.Rel      = TypedCopyAttr::BT_Equivalent;
    Res.TypeExpr = Orig;
    pushScopeSubst(Res);
    return;
  }
  SuperTv::traverseField(Orig);
}



void TypedEvaluator::traverseLet(Let *Orig) {
  if (!Builder.currentCFG()) {
    SuperTv::traverseLet(Orig);
    return;
  }

  // Eliminate the let by substituting for the let-variable.
  traverse(Orig->variableDecl()->definition(), TRV_Decl);
  scope()->enterScope(Orig->variableDecl(), std::move(lastAttr()));

  traverse(Orig->body(), TRV_Tail);
  scope()->exitScope();

  // Return the result of traversing the body.
  resultAttr() = std::move( lastAttr() );
}


void TypedEvaluator::traverseIfThenElse(IfThenElse *Orig) {
  // Just do a normal traversal if we're not currently rewriting in a CFG.
  if (!Builder.currentBB()) {
    SuperTv::traverseIfThenElse(Orig);
    return;
  }

  // End current block with a branch
  traverseArg(Orig->condition());
  Instruction* Nc = dyn_cast<Instruction>(lastAttr().Exp);
  Branch* Br = Builder.newBranch(Nc);

  //Instruction* nci = dyn_cast<Instruction>(nc);
  //if (!nci || nci->baseType().Base != BaseType::BT_Bool)
  //  diag.error("Branch condition is not a boolean: ") << nci;

  // If the current continuation is null, then make a new one.
  BasicBlock* CurrCont = scope()->currentContinuation();
  BasicBlock* Cont     = CurrCont;
  if (!Cont)
    Cont = Builder.newBlock(1);

  // Process the then and else blocks
  Builder.beginBlock(Br->thenBlock());
  scope()->setCurrentContinuation(Cont);
  traverse(Orig->thenExpr(), TRV_Tail);

  Builder.beginBlock(Br->elseBlock());
  scope()->setCurrentContinuation(Cont);
  traverse(Orig->elseExpr(), TRV_Tail);

  scope()->setCurrentContinuation(CurrCont);   // restore old continuation

  // If we had an existing continuation, then we're done.
  // The then/else blocks will call the continuation.
  if (CurrCont)
    return;

  // Otherwise, if we created a new continuation, then start processing it.
  Builder.beginBlock(Cont);
}



void TypedEvaluator::traverseNestedCode(Code* Orig) {
  // Code blocks within a CFG are eliminated; we add them to pendingBlocks.
  // TODO: prevent nested blocks from escaping.
  // TODO: this whole thing is a hack right now.

  traverse(Orig->returnType(), TRV_Type);
  traverseNull();
  reduceCode(Orig);
  Code* Nc = cast<Code>(resultAttr().Exp);

  // Create a new block, and add it to pending blocks.
  // The new block will be enqueued on the first call to it,
  // and the queue will be processed before we leave the current CFG.

  // Create a new scope, where args point to phi nodes in the new block.
  unsigned Nargs = Builder.deBruinIndex() -
      Builder.deBruinIndexOfEnclosingNestedFunction();
  unsigned Vidx  = scope()->numVars() - Nargs;

  auto* Nb = Builder.newBlock(Nargs);
  auto* Ns = scope()->clone();
  for (unsigned i=0; i < Nargs; ++i) {
    // TODO: (FIXME) Hack to deal with self-variables.
    auto &Entry = Ns->entry(Vidx + i);
    if (Entry.VDecl->kind() == VarDecl::VK_SFun)
      continue;
    assert(Entry.VDecl->kind() != VarDecl::VK_Let);

    auto &At    = Entry.VarAttr;
    At.Exp      = Nb->arguments()[i];
    At.Rel      = TypedCopyAttr::BT_Equivalent;
    At.TypeExpr = Nb->arguments()[i];
  }

  // Add pending block.
  auto* Pb = new PendingBlock(Orig->body(), Nb, Ns);
  PendingBlks.emplace_back(Pb);
  CodeMap.insert(std::make_pair(Nc, Pb));
}



bool TypedEvaluator::reduceNestedCall(Call* Orig, Code* C) {
  // See if this is a call to a nested function.
  auto It = CodeMap.find(C);
  if (It == CodeMap.end())
    return false;

  auto& Ca  = attr(0);
  auto& Res = resultAttr();

  PendingBlock *Pb = It->second;
  if (Pb->Cont == nullptr) {
    // Set the continuation of PB to the current continuation,
    // and add PB to the queue.
    if (!scope()->currentContinuation()) {
      diag().error("Call to nested function must be a tail call.") << Orig;
      Res.Exp = Builder.newUndefined();
      return true;
    }
    Pb->Cont = scope()->currentContinuation();
    PendingBlockQueue.push(Pb);
  }
  else if (Pb->Cont != scope()->currentContinuation()) {
    // check that the continuations match.
    diag().error("Calls to nested function are not a valid CFG.") << Orig;
    Res.Exp = Builder.newUndefined();
    return true;
  }
  if (Ca.Subst.size() != Pb->Block->arguments().size()) {
    diag().error("Invalid number of arguments to function call.") << Orig;
    Res.Exp = Builder.newUndefined();
    return true;
  }

  // Insert a Goto to the new block.
  std::vector<SExpr*> Args;
  for (auto& At : Ca.Subst) {
    // TODO: (FIXME) Ugly hack to deal with self-arguments.
    if (isa<Function>(At.Exp)) {
      Args.push_back(nullptr);
      continue;
    }
    if (Variable* V = dyn_cast_or_null<Variable>(At.Exp)) {
      if (V->variableDecl()->kind() == VarDecl::VK_SFun) {
        Args.push_back(nullptr);
        continue;
      }
    }
    Args.push_back(At.Exp);
  }
  Builder.newGoto(Pb->Block, ArrayRef<SExpr*>(&Args[0], Args.size()));

  return true;
}



void TypedEvaluator::processPendingBlocks() {
  while (!PendingBlockQueue.empty()) {
    auto* Pb = PendingBlockQueue.front();
    PendingBlockQueue.pop();

    Builder.beginBlock(Pb->Block);
    Pb->Scope->setCurrentContinuation(Pb->Cont);
    auto *S = switchScope(Pb->Scope);

    traverse(Pb->Exp, TRV_Tail);
    popAttr();

    restoreScope(S);
  }
  PendingBlks.clear();
}



}  // end namespace til
}  // end namespace ohmu





