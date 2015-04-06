//===- TILTraverse.h ------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a framework for doing generic traversals and rewriting
// operations over the Thread Safety TIL.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_TILTRAVERSE_H
#define OHMU_TIL_TILTRAVERSE_H

#include "TIL.h"

namespace ohmu {
namespace til  {

/// TraversalKind describes the location in which a subexpression occurs.
/// The traversal depends on this information, e.g. it should not traverse
/// weak subexpressions, and should not eagerly traverse lazy subexpressions.
enum TraversalKind {
  TRV_Weak,  ///< un-owned (weak) reference to subexpression
  TRV_Arg,   ///< owned subexpr in argument position  e.g. a in  f(a), a+b
  TRV_Instr, ///< owned subexpr as basic block instr
  TRV_Path,  ///< owned subexpr on spine of path      e.g. f in  f(a)
  TRV_Tail,  ///< owned subexpr in tail position      e.g. u in  let x=t; u
  TRV_Decl,  ///< owned subexpr in a declaration      e.g. function body
  TRV_Lazy,  ///< owned subexpr in lazy position      e.g. code body
  TRV_Type   ///< owned subexpr in type position      e.g. T in  \\x:T -> u
};


/// The Traversal class defines an interface for traversing SExprs.  Traversals
/// have been made as generic as possible, and are intended to handle any kind
/// of pass over the AST, e.g. visiters, copiers, non-destructive rewriting,
/// destructive (in-place) rewriting, hashing, typing, garbage collection, etc.
///
/// The Traversal class is responsible for traversing the AST in some order.
/// The default is a depth first traversal, but other orders are possible,
/// such as BFS, lazy or parallel traversals.
///
/// The AST distinguishes between owned sub-expressions, which form a spanning
/// tree, and weak subexpressions, which are internal and possibly cyclic
/// references.  A traversal will recursively traverse owned sub-expressions.
///
/// Subclasses can override the following in order to insert pre and post-visit
/// code around a traversal.  Overridden versions should call the parent.
///
///   * traverse<T>(...) is the entry point for a traversal of an SExpr*.
///   * traverseX(...) is the entry point for a traversal of node class X.
///
/// This class must be combined with other classes to implement the full
/// traversal interface:
///
///   * A Reducer class, e.g. VisitReducer, CopyReducer, or InplaceReducer.
///   * A ScopeHandler (DefaultScopeHandler) to handle lexical scope.
///
/// The Reducer class implements reduceX methods, which are responsible for
/// rewriting terms.  After an SExpr has been traversed, the traversal will
/// call reduceX to construct a result.
///
template <class Self>
class Traversal {
public:
  /// Cast this to the correct type (curiously recursive template pattern.)
  Self *self() { return static_cast<Self *>(this); }

  /// Initial starting point, to be called by external routines.
  void traverseAll(SExpr *E) {
    self()->traverse(E, TRV_Tail);
  }

  /// Invoked by SExpr classes to traverse possibly weak members.
  /// Do not override.
  void traverseArg(SExpr *E) {
    // Detect weak references to other instructions in the CFG.
    if (Instruction *I = E->asCFGInstruction())
      self()->traverseWeak(I);
    else
      self()->traverse(E, TRV_Arg);
  }

  /// Starting point for a traversal.
  /// Override this method to traverse SExprs of arbitrary type.
  template <class T>
  void traverse(T *E, TraversalKind K) {
    traverseByType(E, K);
  }

  /// Invoked by SExpr classes to traverse weak arguments;
  void traverseWeak(Instruction *E) { self()->reduceWeak(E); }

  /// Invoked by SExpr classes to handle null members.
  void traverseNull() {  self()->reduceNull(); }

  /// Override these methods to traverse a particular type of SExpr.
#define TIL_OPCODE_DEF(X)                                                 \
  void traverse##X(X *E);
#include "TILOps.def"


protected:
  /// For generic SExprs, do dynamic dispatch by type.
  void traverseByType(SExpr *E, TraversalKind K) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                 \
    case COP_##X:                                                         \
      self()->traverse##X(cast<X>(E));                                    \
      return;
#include "TILOps.def"
    }
    self()->reduceNull();
  }

  /// For SExprs of known type, do static dispatch by type.
#define TIL_OPCODE_DEF(X)                                                 \
  void traverseByType(X *E, TraversalKind K) {                            \
    return self()->traverse##X(E);                                        \
  }
#include "TILOps.def"
};



/// DefaultScopeHandler implements empty versions of the lexical scope
/// enter/exit routines for traversals.
class DefaultScopeHandler {
public:
  void enterScope(VarDecl *Vd)   { }
  void exitScope (VarDecl *Vd)   { }
  void enterCFG  (SCFG *Cfg)     { }
  void exitCFG   (SCFG *Cfg)     { }
  void enterBlock(BasicBlock *B) { }
  void exitBlock (BasicBlock *B) { }
};


/// DefaultReducer implements empty versions of all of the reduce() methods
/// for a traversal.
class DefaultReducer {
  /// Reduce a null SExpr
  void reduceNull() { }

  /// Reduce a weak reference to a CFG Instruction
  void reduceWeak(Instruction* Orig) { }

  /// Reduce a basic block argument.
  void reduceBBArgument(Phi *Orig) { }

  /// Reduce a basic block instruction.
  void reduceBBInstruction(Instruction *Orig) { }

#define TIL_OPCODE_DEF(X)   \
  void reduce##X(X *E) { }
#include "TILOps.def"
};



////////////////////////////////////////
// traverse methods for all TIL classes.
////////////////////////////////////////

template <class S>
void Traversal<S>::traverseVarDecl(VarDecl *E) {
  switch (E->kind()) {
    case VarDecl::VK_Fun: {
      self()->traverse(E->definition(), TRV_Type);
      self()->reduceVarDecl(E);
      return;
    }
    case VarDecl::VK_SFun: {
      // Don't traverse the definition, since it cyclicly points back to self.
      // Just create a new (dummy) definition.
      self()->traverseNull();
      self()->reduceVarDecl(E);
      return;
    }
    case VarDecl::VK_Let: {
      self()->traverse(E->definition(), TRV_Decl);
      self()->reduceVarDecl(E);
      return;
    }
  }
}

template <class S>
void Traversal<S>::traverseFunction(Function *E) {
  // E is a variable declaration, so traverse the definition.
  self()->traverse(E->variableDecl(), TRV_Decl);
  // Tell the rewriter to enter the scope of the function.
  self()->enterScope(E->variableDecl());
  self()->traverse(E->body(), TRV_Lazy);
  self()->exitScope(E->variableDecl());
  self()->reduceFunction(E);
}

template <class S>
void Traversal<S>::traverseCode(Code *E) {
  self()->traverse(E->returnType(), TRV_Type);
  if (E->body())
    self()->traverse(E->body(), TRV_Lazy);
  else
    self()->traverseNull();
  self()->reduceCode(E);
}

template <class S>
void Traversal<S>::traverseField(Field *E) {
  self()->traverse(E->range(), TRV_Type);
  if (E->body())
    self()->traverse(E->body(),  TRV_Lazy);
  else
    self()->traverseNull();
  self()->reduceField(E);
}

template <class S>
void Traversal<S>::traverseSlot(Slot *E) {
  self()->traverse(E->definition(), TRV_Lazy);
  self()->reduceSlot(E);
}

template <class S>
void Traversal<S>::traverseRecord(Record *E) {
  for (auto &Slt : E->slots()) {
    self()->traverse(Slt.get(), TRV_Decl);
  }
  self()->reduceRecord(E);
}

template <class S>
void Traversal<S>::traverseScalarType(ScalarType *E) {
  self()->reduceScalarType(E);
}


template<class S>
class LitTraverser {
public:
  template<class Ty>
  class Actor {
  public:
    typedef bool ReturnType;
    static bool defaultAction(S* Visitor, Literal *E) {
      return false;
    }
    static bool action(S* Visitor, Literal *E) {
      Visitor->template reduceLiteralT<Ty>(E->as<Ty>());
      return true;
    }
  };
};

template <class S>
void Traversal<S>::traverseLiteral(Literal *E) {
  if (!BtBr< LitTraverser<S>::template Actor >::
        branch(E->baseType(), self(), E))
    self()->reduceLiteral(E);
}

template <class S>
void Traversal<S>::traverseVariable(Variable *E) {
  self()->reduceVariable(E);
}

template <class S>
void Traversal<S>::traverseApply(Apply *E) {
  self()->traverse(E->fun(), TRV_Path);
  E->arg() ? self()->traverseArg(E->arg())
           : self()->traverseNull();
  self()->reduceApply(E);
}

template <class S>
void Traversal<S>::traverseProject(Project *E) {
  self()->traverse(E->record(), TRV_Path);
  self()->reduceProject(E);
}

template <class S>
void Traversal<S>::traverseCall(Call *E) {
  self()->traverse(E->target(), TRV_Path);
  self()->reduceCall(E);
}

template <class S>
void Traversal<S>::traverseAlloc(Alloc *E) {
  self()->traverseArg(E->initializer());
  self()->reduceAlloc(E);
}

template <class S>
void Traversal<S>::traverseLoad(Load *E) {
  self()->traverseArg(E->pointer());
  self()->reduceLoad(E);
}

template <class S>
void Traversal<S>::traverseStore(Store *E) {
  self()->traverseArg(E->destination());
  self()->traverseArg(E->source());
  self()->reduceStore(E);
}

template <class S>
void Traversal<S>::traverseArrayIndex(ArrayIndex *E) {
  self()->traverseArg(E->array());
  self()->traverseArg(E->index());
  self()->reduceArrayIndex(E);
}

template <class S>
void Traversal<S>::traverseArrayAdd(ArrayAdd *E) {
  self()->traverseArg(E->array());
  self()->traverseArg(E->index());
  self()->reduceArrayAdd(E);
}

template <class S>
void Traversal<S>::traverseUnaryOp(UnaryOp *E) {
  self()->traverseArg(E->expr());
  self()->reduceUnaryOp(E);
}

template <class S>
void Traversal<S>::traverseBinaryOp(BinaryOp *E) {
  self()->traverseArg(E->expr0());
  self()->traverseArg(E->expr1());
  self()->reduceBinaryOp(E);
}

template <class S>
void Traversal<S>::traverseCast(Cast *E) {
  self()->traverseArg(E->expr());
  self()->reduceCast(E);
}

template <class S>
void Traversal<S>::traversePhi(Phi *E) {
  // Note: traversing a Phi does not traverse its arguments.
  // The arguments are traversed by the Goto, which is the place where
  // they are within scope.
  self()->reducePhi(E);
}

template <class S>
void Traversal<S>::traverseGoto(Goto *E) {
  unsigned Idx = E->phiIndex();
  for (Phi *Ph : E->targetBlock()->arguments()) {
    // Ignore any newly-added Phi nodes (e.g. from an in-place SSA pass.)
    if (Ph && Ph->instrID() > 0)
      self()->traverseArg( Ph->values()[Idx].get() );
  }
  self()->reduceGoto(E);
}

template <class S>
void Traversal<S>::traverseBranch(Branch *E) {
  self()->traverseArg(E->condition());
  self()->reduceBranch(E);
}

template <class S>
void Traversal<S>::traverseReturn(Return *E) {
  self()->traverseArg(E->returnValue());
  self()->reduceReturn(E);
}

template <class S>
void Traversal<S>::traverseBasicBlock(BasicBlock *E) {
  self()->enterBlock(E);
  for (Phi *A : E->arguments()) {
    self()->traverse(A, TRV_Instr);
    self()->reduceBBArgument(A);
  }

  for (Instruction *I : E->instructions()) {
    self()->traverse(I, TRV_Instr);
    self()->reduceBBInstruction(I);
  }

  self()->traverse(E->terminator(), TRV_Instr);
  self()->reduceBasicBlock(E);
  self()->exitBlock(E);
}

template <class S>
void Traversal<S>::traverseSCFG(SCFG *E) {
  self()->enterCFG(E);
  for (auto &B : E->blocks())
    self()->traverse(B.get(), TRV_Decl);
  self()->reduceSCFG(E);
  self()->exitCFG(E);
}

template <class S>
void Traversal<S>::traverseFuture(Future *E) {
  SExpr* Res = E->force();
  self()->traverse(Res, TRV_Decl);
}

template <class S>
void Traversal<S>::traverseUndefined(Undefined *E) {
  self()->reduceUndefined(E);
}

template <class S>
void Traversal<S>::traverseWildcard(Wildcard *E) {
  self()->reduceWildcard(E);
}

template <class S>
void Traversal<S>::traverseIdentifier(Identifier *E) {
  self()->reduceIdentifier(E);
}

template <class S>
void Traversal<S>::traverseLet(Let *E) {
  // E is a variable declaration, so traverse the definition.
  self()->traverse(E->variableDecl(), TRV_Decl);
  // Tell the rewriter to enter the scope of the let variable.
  self()->enterScope(E->variableDecl());
  self()->traverse(E->body(), TRV_Arg);
  self()->exitScope(E->variableDecl());
  self()->reduceLet(E);
}

template <class S>
void Traversal<S>::traverseIfThenElse(IfThenElse *E) {
  self()->traverseArg(E->condition());
  self()->traverse(E->thenExpr(), TRV_Arg);
  self()->traverse(E->elseExpr(), TRV_Arg);
  self()->reduceIfThenElse(E);
}


} // end namespace til
} // end namespace ohmu

#endif  // OHMU_TIL_TILTRAVERSE_H
