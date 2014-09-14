//===- ThreadSafetyTraverse.h ----------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a framework for doing generic traversals and rewriting
// operations over the Thread Safety TIL.
//
// UNDER CONSTRUCTION.  USE AT YOUR OWN RISK.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTRAVERSE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTRAVERSE_H

#include "ThreadSafetyTIL.h"

namespace clang {
namespace threadSafety {
namespace til {

// Defines an interface used to traverse SExprs.  Traversals have been made as
// generic as possible, and are intended to handle any kind of pass over the
// AST, e.g. visiters, copying, non-destructive rewriting, destructive
// (in-place) rewriting, hashing, typing, etc.
//
// Traversals implement the functional notion of a "fold" operation on SExprs.
// Each SExpr class provides a traverse method, which does the following:
//   * e->traverse(v):
//       // compute a result r_i for each subexpression e_i
//       for (i = 1..n)  r_i = v.traverse(e_i);
//       // combine results into a result for e,  where X is the class of e
//       return v.reduceX(*e, r_1, .. r_n).
//
// A visitor can control the traversal by overriding the following methods:
//   * v.traverse(e):
//       return v.traverseByCase(e), which returns v.traverseX(e)
//   * v.traverseX(e):   (X is the class of e)
//       return e->traverse(v).
//   * v.reduceX(*e, r_1, .. r_n):
//       compute a result for a node of type X
//
// The reduceX methods control the kind of traversal (visitor, copy, etc.).
// They are defined in derived classes.
//
// Class R defines the basic interface types (R_SExpr).
template <class Self, class R>
class Traversal {
public:
  Self *self() { return static_cast<Self *>(this); }

  // Override this method to do something for every expression.
  typename R::R_SExpr traverse(SExpr *E, typename R::R_Ctx Ctx) {
    return traverseByCase(E, Ctx);
  }

  // Helper method to call traverseX(e) on the appropriate type.
  typename R::R_SExpr traverseByCase(SExpr *E, typename R::R_Ctx Ctx) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                   \
    case COP_##X:                                                           \
      return self()->traverse##X(cast<X>(E), Ctx);
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return self()->reduceNull();
  }

// Traverse e, by static dispatch on the type "X" of e.
// Override these methods to do something for a particular kind of term.
#define TIL_OPCODE_DEF(X)                                                   \
  typename R::R_SExpr traverse##X(X *e, typename R::R_Ctx Ctx) {            \
    return e->traverse(*self(), Ctx);                                       \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
};


// Base class for simple reducers that don't much care about the context.
class SimpleReducerBase {
public:
  enum TraversalKind {
    TRV_Normal,   // ordinary subexpressions
    TRV_Decl,     // declarations (e.g. function bodies)
    TRV_Lazy,     // expressions that require lazy evaluation
    TRV_Type      // type expressions
  };

  // R_Ctx defines a "context" for the traversal, which encodes information
  // about where a term appears.  This can be used to encoding the
  // "current continuation" for CPS transforms, or other information.
  typedef TraversalKind R_Ctx;

  // Create context for an ordinary subexpression.
  R_Ctx subExprCtx(R_Ctx Ctx) { return TRV_Normal; }

  // Create context for a subexpression that occurs in a declaration position
  // (e.g. function body).
  R_Ctx declCtx(R_Ctx Ctx) { return TRV_Decl; }

  // Create context for a subexpression that occurs in a position that
  // should be reduced lazily.  (e.g. code body).
  R_Ctx lazyCtx(R_Ctx Ctx) { return TRV_Lazy; }

  // Create context for a subexpression that occurs in a type position.
  R_Ctx typeCtx(R_Ctx Ctx) { return TRV_Type; }
};


// Base class for traversals that rewrite an SExpr to another SExpr.
class CopyReducerBase : public SimpleReducerBase {
public:
  // R_SExpr is the result type for a traversal.
  // A copy or non-destructive rewrite returns a newly allocated term.
  typedef SExpr *R_SExpr;
  typedef BasicBlock *R_BasicBlock;

  // Container is a minimal interface used to store results when traversing
  // SExprs of variable arity, such as Phi, Goto, and SCFG.
  template <class T> class Container {
  public:
    // Allocate a new container with a capacity for n elements.
    Container(CopyReducerBase &S, unsigned N) : Elems(S.Arena, N) {}

    // Push a new element onto the container.
    void push_back(T E) { Elems.push_back(E); }

    SimpleArray<T> Elems;
  };

  CopyReducerBase(MemRegionRef A) : Arena(A) {}

protected:
  MemRegionRef Arena;
};


// Base class for visit traversals.
class VisitReducerBase : public SimpleReducerBase {
public:
  // A visitor returns a bool, representing success or failure.
  typedef bool R_SExpr;
  typedef bool R_BasicBlock;

  // A visitor "container" is a single bool, which accumulates success.
  template <class T> class Container {
  public:
    Container(VisitReducerBase &S, unsigned N) : Success(true) {}
    void push_back(bool E) { Success = Success && E; }

    bool Success;
  };
};


// Implements a traversal that visits each subexpression, and returns either
// true or false.
template <class Self>
class VisitReducer : public Traversal<Self, VisitReducerBase>,
                     public VisitReducerBase {
public:
  VisitReducer() {}

public:
  R_SExpr reduceNull() { return true; }
  R_SExpr reduceUndefined(Undefined &Orig) { return true; }
  R_SExpr reduceWildcard(Wildcard &Orig) { return true; }

  R_SExpr reduceVarDecl(VarDecl &Orig, R_SExpr E) { return true; }

  R_SExpr reduceLiteral(Literal &Orig) { return true; }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) { return true; }
  R_SExpr reduceLiteralPtr(Literal &Orig) { return true; }

  R_SExpr reduceFunction(Function &Orig, VarDecl *Nvd, R_SExpr E0) {
    return Nvd && E0;
  }
  R_SExpr reduceSFunction(SFunction &Orig, VarDecl *Nvd, R_SExpr E0) {
    return Nvd && E0;
  }
  R_SExpr reduceCode(Code &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceField(Field &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceProject(Project &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceCall(Call &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceAlloc(Alloc &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceLoad(Load &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceStore(Store &Orig, R_SExpr E0, R_SExpr E1) { return E0 && E1; }
  R_SExpr reduceArrayIndex(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceArrayAdd(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceUnaryOp(UnaryOp &Orig, R_SExpr E0) { return E0; }
  R_SExpr reduceBinaryOp(BinaryOp &Orig, R_SExpr E0, R_SExpr E1) {
    return E0 && E1;
  }
  R_SExpr reduceCast(Cast &Orig, R_SExpr E0) { return E0; }

  R_SExpr reduceSCFG(SCFG &Orig, Container<BasicBlock *> Bbs) {
    return Bbs.Success;
  }
  R_BasicBlock reduceBasicBlock(BasicBlock &Orig, Container<R_SExpr> &As,
                                Container<R_SExpr> &Is, R_SExpr T) {
    return (As.Success && Is.Success && T);
  }
  R_SExpr reducePhi(Phi &Orig, Container<R_SExpr> &As) {
    return As.Success;
  }
  R_SExpr reduceGoto(Goto &Orig, BasicBlock *B) {
    return true;
  }
  R_SExpr reduceBranch(Branch &O, R_SExpr C, BasicBlock *B0, BasicBlock *B1) {
    return C;
  }
  R_SExpr reduceReturn(Return &O, R_SExpr E) {
    return E;
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    return true;
  }
  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return C && T && E;
  }
  R_SExpr reduceLet(Let &Orig, VarDecl *Nvd, R_SExpr B) {
    return Nvd && B;
  }

  VarDecl *enterScope(VarDecl &Orig, R_SExpr E0) { return &Orig; }
  void exitScope(const VarDecl &Orig) {}
  void enterCFG(SCFG &Cfg) {}
  void exitCFG(SCFG &Cfg) {}
  void enterBasicBlock(BasicBlock &BB) {}
  void exitBasicBlock(BasicBlock &BB) {}

  VarDecl    *reduceVariableRef  (VarDecl *Ovd)   { return Ovd; }
  BasicBlock *reduceBasicBlockRef(BasicBlock *Obb) { return Obb; }

public:
  bool traverse(SExpr *E, TraversalKind K = TRV_Normal) {
    Success = Success && this->traverseByCase(E);
    return Success;
  }

  static bool visit(SExpr *E) {
    Self Visitor;
    return Visitor.traverse(E, TRV_Normal);
  }

private:
  bool Success;
};


// Basic class for comparison operations over expressions.
template <typename Self>
class Comparator {
protected:
  Self *self() { return reinterpret_cast<Self *>(this); }

public:
  bool compareByCase(const SExpr *E1, const SExpr* E2) {
    switch (E1->opcode()) {
#define TIL_OPCODE_DEF(X)                                                     \
    case COP_##X:                                                             \
      return cast<X>(E1)->compare(cast<X>(E2), *self());
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return false;
  }
};


class EqualsComparator : public Comparator<EqualsComparator> {
public:
  // Result type for the comparison, e.g. bool for simple equality,
  // or int for lexigraphic comparison (-1, 0, 1).  Must have one value which
  // denotes "true".
  typedef bool CType;

  CType trueResult() { return true; }
  bool notTrue(CType ct) { return !ct; }

  bool compareIntegers(unsigned i, unsigned j)       { return i == j; }
  bool compareStrings (StringRef s, StringRef r)     { return s == r; }
  bool comparePointers(const void* P, const void* Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr* E2) {
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const VarDecl* V1, const VarDecl* V2) { }
  void leaveScope() { }

  bool compareVariableRefs(const VarDecl* V1, const VarDecl* V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    EqualsComparator Eq;
    return Eq.compare(E1, E2);
  }
};



class MatchComparator : public Comparator<MatchComparator> {
public:
  // Result type for the comparison, e.g. bool for simple equality,
  // or int for lexigraphic comparison (-1, 0, 1).  Must have one value which
  // denotes "true".
  typedef bool CType;

  CType trueResult() { return true; }
  bool notTrue(CType ct) { return !ct; }

  bool compareIntegers(unsigned i, unsigned j)       { return i == j; }
  bool compareStrings (StringRef s, StringRef r)     { return s == r; }
  bool comparePointers(const void* P, const void* Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr* E2) {
    // Wildcards match anything.
    if (E1->opcode() == COP_Wildcard || E2->opcode() == COP_Wildcard)
      return true;
    // otherwise normal equality.
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const VarDecl* V1, const VarDecl* V2) { }
  void leaveScope() { }

  bool compareVariableRefs(const VarDecl* V1, const VarDecl* V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    MatchComparator Matcher;
    return Matcher.compare(E1, E2);
  }
};

} // end namespace til
} // end namespace threadSafety
} // end namespace clang

#endif  // LLVM_CLANG_THREAD_SAFETY_TRAVERSE_H
