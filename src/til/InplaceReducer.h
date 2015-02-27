//===- InplaceReducer.h ----------------------------------------*- C++ --*-===//
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


#ifndef OHMU_TIL_INPLACEREDUCER_H
#define OHMU_TIL_INPLACEREDUCER_H

#include "AttributeGrammar.h"
#include "CopyReducer.h"
#include "CFGBuilder.h"


namespace ohmu {
namespace til  {


/// InplaceReducer implements the reducer interface so that each reduce
/// modifies the original return in-place, and returns a pointer to the
/// original term.  It is intended to be used as a basic class for destructive
/// in-place transformations, such as SSA conversion.
template<class Attr = CopyAttr, class ScopeT = CopyScope<Attr>>
class InplaceReducer : public AttributeGrammar<Attr, ScopeT> {
public:
  void enterScope(VarDecl *Vd) {
    // enterScope must be called after reduceVarDecl()
    auto* Nvd = cast<VarDecl>( this->lastAttr().Exp );
    auto* Nv  = Builder.newVariable(Nvd);

    // Variables that point to Orig will be replaced with Nv.
    this->scope()->enterScope(Vd, Attr(Nv));
    Builder.enterScope(Nvd);
  }

  void exitScope(VarDecl *Vd) {
    Builder.exitScope();
    this->scope()->exitScope();
  }

  void enterCFG(SCFG *Cfg) {
    Builder.beginCFG(nullptr, Cfg->numBlocks(), Cfg->numInstructions());
    this->scope()->enterCFG(Cfg, Builder.currentCFG());
  }

  void exitCFG(SCFG *Cfg) {
    Builder.currentCFG()->renumber();
    Builder.endCFG();
    this->scope()->exitCFG();
  }

  void enterBlock(BasicBlock *B) {
    Builder.beginBlock( getBasicBlock(B) );
  }

  void exitBlock(BasicBlock *B) {
    // Sanity check; the terminator should end the block.
    if (Builder.currentBB())
      Builder.endBlock(nullptr);
  }

  /// Find the basic block that Orig maps to, or create a new one.
  BasicBlock* getBasicBlock(BasicBlock *Orig) { return Orig; }

  /*--- Reduce Methods ---*/

  void reduceNull() {
    this->resultAttr().Exp = nullptr;
  }

  void reduceWeak(Instruction *Orig) {
    // Map weak references to rewritten instructions.
    this->resultAttr().Exp = this->scope()->lookupInstr(Orig).Exp;
  }

  void reduceVarDecl(VarDecl *Orig) {
    auto *E = this->attr(0).Exp;
    Orig->rewrite(E);
    this->resultAttr().Exp = Orig;
  }

  void reduceFunction(Function *Orig) {
    VarDecl *Nvd = cast<VarDecl>(this->attr(0).Exp);
    auto *E0 = this->attr(1).Exp;
    Orig->rewrite(Nvd, E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceCode(Code *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceField(Field *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceSlot(Slot *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceRecord(Record *Orig) {
    unsigned Ns = this->numAttrs();
    assert(Ns == Orig->slots().size());
    for (unsigned i = 0; i < Ns; ++i) {
      Slot *S = cast<Slot>( this->attr(i).Exp );
      Orig->slots()[i] = S;
    }
    this->resultAttr().Exp = Orig;
  }

  void reduceScalarType(ScalarType *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceLiteral(Literal *Orig) {
    this->resultAttr().Exp = Orig;
  }

  template<class T>
  void reduceLiteralT(LiteralT<T> *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceVariable(Variable *Orig) {
    this->resultAttr() = Orig;
  }

  void reduceApply(Apply *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceProject(Project *Orig) {
    auto *E0  = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceCall(Call *Orig) {
    auto *E0  = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceAlloc(Alloc *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceLoad(Load *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceStore(Store *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceArrayIndex(ArrayIndex *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceArrayAdd(ArrayAdd *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceUnaryOp(UnaryOp *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceBinaryOp(BinaryOp *Orig) {
    auto *E0 = this->attr(0).Exp;
    auto *E1 = this->attr(1).Exp;
    Orig->rewrite(E0, E1);
    this->resultAttr().Exp = Orig;
  }

  void reduceCast(Cast *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    this->resultAttr().Exp = Orig;
  }

  void reducePhi(Phi *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceGoto(Goto *Orig) {
    BasicBlock *B = Orig->targetBlock();

    // All "arguments" to the Goto should have been pushed onto the stack.
    // Write them to their appropriate Phi nodes.
    assert(B->arguments().size() == this->numAttrs());
    unsigned i = 0;
    for (Phi *Ph : B->arguments()) {
      Builder.setPhiArgument(Ph, this->attr(i).Exp, Idx);
      ++i;
    }

    Builder.endBlock(Orig);
    this->resultAttr().Exp = Orig;
  }

  void reduceBranch(Branch *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0);
    Builder.endBlock(Orig);
  }

  void reduceReturn(Return *Orig) {
    auto *E0 = this->attr(0).Exp;
    Orig->rewrite(E0, E1);
    Builder.endBlock(Orig);
  }

  void reduceBasicBlock(BasicBlock *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceSCFG(SCFG *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceUndefined(Undefined *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceWildcard(Wildcard *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceIdentifier(Identifier *Orig) {
    this->resultAttr().Exp = Orig;
  }

  void reduceLet(Let *Orig) {
    VarDecl *Nvd = cast<VarDecl>( this->attr(0).Exp );
    auto    *E0  = this->attr(1).Exp;
    Orig->rewrite(Nvd, E0);
    this->resultAttr().Exp = Orig;
  }

  void reduceIfThenElse(IfThenElse *Orig) {
    auto *C = this->attr(0).Exp;
    auto *T = this->attr(1).Exp;
    auto *E = this->attr(2).Exp;
    Orig->rewrite(C, T, E);
    this->resultAttr().Exp = Orig;
  }

public:
  InplaceReducer()
    : AttributeGrammar<Attr, ScopeT>(new ScopeT())
  { }
  InplaceReducer(MemRegionRef A)
    : AttributeGrammar<Attr, ScopeT>(new ScopeT()), Builder(A)
  { }

protected:
  CFGBuilder Builder;
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_INPLACEREDUCER_H
