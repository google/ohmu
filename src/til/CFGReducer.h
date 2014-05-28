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

#include <vector>

namespace ohmu {

using namespace clang::threadSafety::til;


class CFGReducerBase {
public:
  enum TraversalKind {
    TRV_Normal,
    TRV_Decl,
    TRV_Lazy,
    TRV_Type
  };

  struct Context {
    TraversalKind Kind;
    BasicBlock *Continuation;

    Context(TraversalKind K, BasicBlock *C) : Kind(K), Continuation(C) {}
  };

  typedef Context R_Ctx;

  R_Ctx subExprCtx(const R_Ctx& Ctx) { return Context(TRV_Normal, nullptr); }
  R_Ctx declCtx   (const R_Ctx& Ctx) { return Context(TRV_Decl,   nullptr); }
  R_Ctx lazyCtx   (const R_Ctx& Ctx) { return Context(TRV_Lazy,   nullptr); }
  R_Ctx typeCtx   (const R_Ctx& Ctx) { return Context(TRV_Type,   nullptr); }

  // R_SExpr is the result type for a traversal.
  // A copy or non-destructive rewrite returns a newly allocated term.
  typedef SExpr *R_SExpr;
  typedef BasicBlock *R_BasicBlock;

  // Container is a minimal interface used to store results when traversing
  // SExprs of variable arity, such as Phi, Goto, and SCFG.
  template <class T> class Container {
  public:
    // Allocate a new container with a capacity for n elements.
    Container(CFGReducerBase &S, unsigned N) : Elems(S.Arena, N) {}

    // Push a new element onto the container.
    void push_back(T E) { Elems.push_back(E); }

    SimpleArray<T> Elems;
  };

  CFGReducerBase(MemRegionRef A) : Arena(A) { }

protected:
  MemRegionRef Arena;
};


template <class Self>
class CFGReducer : public Traversal<Self, CFGReducerBase>,
                   public CFGReducerBase {
public:
  SCFG* convertToCFG(SExpr *E) {
    assert(currentCFG == nullptr && currentBB == nullptr);
    currentCFG = new (Arena) SCFG(Arena, 0);
    currentBB = currentCFG->entry();
    traverse(E, Context(TRV_Normal, currentCFG->exit()));
    currentCFG->renumberVars();
    return currentCFG;
  }

  R_SExpr traverse(SExprRef &E, R_Ctx Ctx) {
    return traverse(E.get(), Ctx);
  }

  // Lower SExpr, writing intermediate results to the current basic block.
  // If Ctx.Continuation is true, then terminate the basic block, by passing
  // the result to the continuation.
  R_SExpr traverse(SExpr *E, R_Ctx Ctx) {
    R_SExpr Result = this->self()->traverseByCase(E, Ctx);
    if (!Ctx.Continuation)
      return Result;  // No continuation.  Continue with current basic block.
    if (!currentBB)
      return Result;  // No current basic block.  Rewrite expressions in place.
    terminateWithGoto(Result, Ctx.Continuation);
    return nullptr;
  }


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
  R_SExpr reduceArrayIndex(ArrayIndex &Orig, R_SExpr E0, R_SExpr E1) {
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
  R_SExpr reduceGoto(Goto &Orig, BasicBlock *B) {
    return new (Arena) Goto(Orig, B, 0);
  }
  R_SExpr reduceBranch(Branch &O, R_SExpr C, BasicBlock *B0, BasicBlock *B1) {
    return new (Arena) Branch(O, C, B0, B1, 0, 0);
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    return new (Arena) Identifier(Orig);
  }
  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return new (Arena) IfThenElse(Orig, C, T, E);
  }
  R_SExpr reduceLet(Let &Orig, Variable *Nvd, R_SExpr B) {
    return new (Arena) Let(Orig, Nvd, B);
  }

  // We have to trap IfThenElse on the traverse rather than reduce, since it
  // the then/else expressions must be evaluated in different basic blocks.
  R_SExpr traverseIfThenElse(IfThenElse *E, Context Ctx) {
    if (!currentBB) {
      // Just do a normal traversal if we're not currently rewriting in a CFG.
      return E->traverse(*this->self(), Ctx);
    }

    SExpr* Nc = addLetVar(
      this->self()->traverse(E->condition(), subExprCtx(Ctx)) );

    // Create new basic blocks for then and else.
    BasicBlock *Ntb = new (Arena) BasicBlock(Arena, currentBB);
    BasicBlock *Neb = new (Arena) BasicBlock(Arena, currentBB);

    // Create a continuation if we don't already have one.
    BasicBlock *Ncb = Ctx.Continuation;
    Variable *NcbArg = nullptr;
    if (!Ncb) {
      Ncb = new (Arena) BasicBlock(Arena, currentBB);
      NcbArg = new (Arena) Variable(new (Arena) Phi());
      Ncb->addArgument(NcbArg);
    }

    // Terminate current basic block with a branch
    unsigned IdxT = Ntb->addPredecessor(currentBB);
    unsigned IdxE = Neb->addPredecessor(currentBB);
    SExpr *Nt = new (Arena) Branch(Nc, Ntb, Neb, IdxT, IdxE);
    terminateCurrentBB(Nt);

    // Rewrite then and else in new blocks
    startBB(Ntb);
    this->self()->traverse(E->thenExpr(), Context(TRV_Normal, Ncb));

    startBB(Neb);
    this->self()->traverse(E->elseExpr(), Context(TRV_Normal, Ncb));

    if (Ctx.Continuation)
      return nullptr;

    // Jump to the newly created continuation
    startBB(Ncb);
    return NcbArg;
  }


  // Create a new variable from orig, and push it onto the lexical scope.
  Variable *enterScope(Variable &Orig, R_SExpr E0) {
    Variable* Nv = new (Arena) Variable(Orig, E0);
    return Nv;
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
  SExpr* addLetVar(SExpr* E) {
    if (!currentBB || !E || ThreadSafetyTIL::isTrivial(E))
      return E;
    Variable* Nv = new (Arena) Variable(E);
    currentInstrs.push_back(Nv);
    return Nv;
  }

  // Start a new basic block, and traverse E.
  void startBB(BasicBlock *BB) {
    assert(currentBB == nullptr);
    assert(currentArgs.empty());
    assert(currentInstrs.empty());

    currentBB = BB;
    currentCFG->add(BB);
  }

  // Finish the current basic block, terminating it with Term.
  void terminateCurrentBB(SExpr* Term) {
    assert(currentBB);
    assert(currentBB->instructions().size() == 0);

    currentBB->instructions().reserve(currentInstrs.size(), Arena);
    currentBB->instructions().append(currentArgs.begin(), currentArgs.end());
    currentBB->setTerminator(Term);
    currentArgs.clear();
    currentInstrs.clear();
    currentBB = nullptr;
  }

  // If the current basic block exists, terminate it with a goto to the
  // target continuation.  Result is passed as an argument to the continuation.
  void terminateWithGoto(SExpr* Result, BasicBlock *Target) {
    assert(currentBB);
    assert(Target->arguments().size() > 0);

    Result = addLetVar(Result);
    unsigned Idx = Target->addPredecessor(currentBB);
    Variable *V = Target->arguments()[0];
    if (Phi *Ph = dyn_cast<Phi>(V->definition())) {
      Ph->values()[Idx] = Result;
    }

    SExpr *Term = new (Arena) Goto(Target, Idx);
    terminateCurrentBB(Term);
  }

  CFGReducer(MemRegionRef A)
     : CFGReducerBase(A), currentCFG(nullptr), currentBB(nullptr) {}

protected:
  SCFG*       currentCFG;                 // the current SCFG
  BasicBlock* currentBB;                  // the current basic block
  std::vector<Variable*> currentArgs;     // arguments in currentBB.
  std::vector<Variable*> currentInstrs;   // instructions in currentBB.
};


class CFGLoweringPass : public CFGReducer<CFGLoweringPass> {
public:
  CFGLoweringPass(MemRegionRef A) : CFGReducer(A) {}

  static SCFG* convertSExprToCFG(SExpr *E, MemRegionRef A) {
    CFGLoweringPass L(A);
    return L.convertToCFG(E);
  }
};


}  // end namespace ohmu

#endif  // OHMU_CFG_REDUCER_H

