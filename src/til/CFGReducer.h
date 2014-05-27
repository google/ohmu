//===- CFGReducer.h --------------------------------------------*- C++ --*-===//
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
//
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_CFG_REDUCER_H
#define OHMU_CFG_REDUCER_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"


namespace ohmu {

using namespace clang::threadSafety::til;


template <class Self>
class CFGReducer : public Traversal<Self, CopyReducerBase>.
                   public CopyReducerBase {
public:
  struct PendingBlock {
    BasicBlock* BB;             // The basic block to finish
    SExpr*      Exp;            // The expression that the block computes
    BasicBlock* Continuation;   // The continuation for the block

    PendingBlock(BasicBlock *B, SExpr *E, BasicBlock *C)
        : BB(B), Exp(E), Continuation(C)
    { }
  };


  R_SExpr reduceNull() {
    return nullptr;
  }
  // R_SExpr reduceFuture(...)  is never used.

  R_SExpr reduceUndefined(Undefined &Orig) {
    return new (Arena) Undefined(Orig);
  }
  R_SExpr reduceWildcard(Wildcard &Orig) {
    return new (Arena) Wildcard(Orig);
  }

  R_SExpr reduceLiteral(Literal &Orig) {
    return new (Arena) Literal(Orig);
  }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) {
    return new (Arena) LiteralT<T>(Orig);
  }
  R_SExpr reduceLiteralPtr(LiteralPtr &Orig) {
    return new (Arena) LiteralPtr(Orig);
  }

  R_SExpr reduceFunction(Function &Orig, Variable *Nvd, R_SExpr E0) {
    return new (Arena) Function(Orig, Nvd, E0);
  }
  R_SExpr reduceSFunction(SFunction &Orig, Variable *Nvd, R_SExpr E0) {
    return new (Arena) SFunction(Orig, Nvd, E0);
  }
  R_SExpr reduceCode(Code &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) Code(Orig, E0, E1);
  }
  R_SExpr reduceField(Field &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) Field(Orig, E0, E1);
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) Apply(Orig, E0, addLetVar(E1));
  }
  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) SApply(Orig, addLetVar(E0), addLetVar(E1));
  }
  R_SExpr reduceProject(Project &Orig, R_SExpr E0) {
    return new (Arena) Project(Orig, E0);
  }
  R_SExpr reduceCall(Call &Orig, R_SExpr E0) {
    return new (Arena) Call(Orig, E0);
  }

  R_SExpr reduceAlloc(Alloc &Orig, R_SExpr E0) {
    return new (Arena) Alloc(Orig, addLetVar(E0));
  }
  R_SExpr reduceLoad(Load &Orig, R_SExpr E0) {
    return new (Arena) Load(Orig, addLetVar(E0));
  }
  R_SExpr reduceStore(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) Store(Orig, addLetVar(E0), addLetVar(E1));
  }
  R_SExpr reduceArrayIndex(ArrayFirst &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) ArrayIndex(Orig, addLetVar(E0), addLetVar(E1));
  }
  R_SExpr reduceArrayAdd(ArrayAdd &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) ArrayAdd(Orig, addLetVar(E0), addLetVar(E1));
  }
  R_SExpr reduceUnaryOp(UnaryOp &Orig, R_SExpr E0) {
    return new (Arena) UnaryOp(Orig, addLetVar(E0));
  }
  R_SExpr reduceBinaryOp(BinaryOp &Orig, R_SExpr E0, R_SExpr E1) {
    return new (Arena) BinaryOp(Orig, addLetVar(E0), addLetVar(E1));
  }
  R_SExpr reduceCast(Cast &Orig, R_SExpr E0) {
    return new (Arena) Cast(Orig, addLetVar(E0));
  }

  R_SExpr reduceSCFG(SCFG &Orig, Container<BasicBlock *> &Bbs) {
    return new (Arena) SCFG(Orig, std::move(Bbs.Elems));
  }
  R_BasicBlock reduceBasicBlock(BasicBlock &Orig, Container<Variable *> &As,
                                Container<Variable *> &Is, R_SExpr T) {
    return new (Arena) BasicBlock(Orig, std::move(As.Elems),
                                        std::move(Is.Elems), T);
  }
  R_SExpr reducePhi(Phi &Orig, Container<R_SExpr> &As) {
    return addLetVar(new (Arena) Phi(Orig, std::move(As.Elems)));
  }
  R_SExpr reduceGoto(Goto &Orig, BasicBlock *B, unsigned Index) {
    return new (Arena) Goto(Orig, B, Index);
  }
  R_SExpr reduceBranch(Branch &O, R_SExpr C, BasicBlock *B0, BasicBlock *B1) {
    return new (Arena) Branch(O, C, B0, B1);
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    return nullptr;
  }

  // We have to trap IfThenElse on the traverse rather than reduce, since it
  // the then/else expressions must be evaluated in different basic blocks.
  R_SExpr traverseIfThenElse(IfThenElse *E) {
    SExpr* Nc = addLetVar(Visitor.traverse(condition()));

    // Create new basic blocks for then, else, and continuation.
    BasicBlock *Ntb = new (Arena) BasicBlock(currentBB);
    BasicBlock *Neb = new (Arena) BasicBlock(currentBB);
    BasicBlock *Ncb = new (Arena) BasicBlock(currentBB);
    Ncb->arguments.reserve(1);  // FIXME

    // Terminate the current block with a branch
    BasicBlock *Cont = currentContinuation;
    SExpr *Nt = new (Arena) Branch(Nc, Ntb, Neb);
    finishCurrentBB(Nt);

    startBB(Ntb, Ncb);
    self()->traverseAndFinishBB(E->thenExpr());

    startBB(Neb, Ncb);
    self()->traverseAndFinishBB(E->elseExpr());

    startBB(Ncb, Cont);

  }


  R_SExpr traverseLet(Let* E) {
    // This is a variable declaration, so traverse the definition.
    SExpr *E0 = self()->traverse(E->variableDecl()->definition());
    Variable *Nvd = Visitor.enterScope(E->variableDecl(), E0);
    typename V::R_SExpr E1 = Visitor.traverse(Body);
    Visitor.exitScope(*VarDecl);
    return Visitor.reduceLet(*this, Nvd, E1);
  }







  // Create a new variable from orig, and push it onto the lexical scope.
  Variable *enterScope(Variable &Orig, R_SExpr E0) {
    Variable* nv = new (Arena) Variable(Orig, E0);

  }
  // Exit the lexical scope of orig.
  void exitScope(const Variable &Orig) {}

  void enterCFG(SCFG &Cfg) {}
  void exitCFG(SCFG &Cfg) {}
  void enterBasicBlock(BasicBlock &BB) {}
  void exitBasicBlock(BasicBlock &BB) {}

  // Map Variable references to their rewritten definitions.
  Variable *reduceVariableRef(Variable *Ovd) { return Ovd; }

  // Map BasicBlock references to their rewritten defs.
  BasicBlock *reduceBasicBlockRef(BasicBlock *Obb) { return Obb; }

protected:
  Variable* addLetVar(SExpr* E) {
    if (!currentBB)
      return E;
    if (!E)
      return nullptr;
    if (til::ThreadSafetyTIL::isTrivial(E))
      return E;
    Variable* Nv = new Variable(E);
    currentInstrs.push_back(Nv);
    return Nv;
  }

  // Start a new basic block, and traverse E.
  void startBB(BasicBlock *BB, BasicBlock *Cont) {
    assert(currentBB == nullptr);
    assert(currentArgs.empty());
    assert(currentInstrs.empty());

    currentBB = BB;
    currentBB->setBlockID(currentBlockID++);
    currentContinuation = Cont;
  }


  // Finish the current basic block, setting T as the terminator.
  void finishCurrentBB(SExpr* T) {
    currentBB->arguments().reserve(currentArgs.size(), Arena);
    currentBB->arguments().append(currentArgs.begin(), currentArgs.end());
    currentBB->instructions().reserve(currentInstrs.size(), Arena);
    currentBB->instructions().append(currentArgs.begin(), currentArgs.end());
    currentBB->setTerminator(T);
    currentArgs.clear();
    currentInstrs.clear();
    currentBB = nullptr;
  }

  CFGReducer(MemRegionRef A) : Arena(A) {}

protected:
  SCFG*       currentCFG;                 // the current SCFG
  BasicBlock* currentBB;                  // the current basic block
  BasicBlock* currentContinuation;        // the continuation for currentBB.
  unsigned    currentBlockID;             // the next available block ID

  std::vector<Variable*> currentArgs;     // arguments in currentBB.
  std::vector<Variable*> currentInstrs;   // instructions in currentBB.
};


}  // end namespace ohmu

#endif  // OHMU_CFG_REDUCER_H

