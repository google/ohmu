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

/// TraversalKind describes the location in which a subexpression occurs.
/// The traversal depends on this information, e.g. it should not traverse
/// weak subexpressions, and should not eagerly traverse lazy subexpressions.
enum TraversalKind {
  TRV_Weak,     ///< un-owned (weak) reference to subexpression
  TRV_SubExpr,  ///< owned subexpression
  TRV_Path,     ///< owned subexpression on spine of path
  TRV_Tail,     ///< owned subexpression in tail position
  TRV_Lazy,     ///< owned subexpression in lazy position
  TRV_Type      ///< owned subexpression in type position
};


/// The Traversal class defines an interface for traversing SExprs.  Traversals
/// have been made as generic as possible, and are intended to handle any kind
/// of pass over the AST, e.g. visiters, copiers, non-destructive rewriting,
/// destructive (in-place) rewriting, hashing, typing, garbage collection, etc.
///
/// The Traversal class is responsible for traversing the AST in some order.
/// The default is a depth first traversal, but other orders are possible,
/// such as lazy or parallel traversals.
///
/// The AST distinguishes between owned sub-expressions, which form a spanning
/// tree, and weak subexpressions, which are internal and possibly cyclic
/// references.  A traversal will recursively traverse owned sub-expressions.
///
/// A traversal keeps a pointer to a Reducer object, which processes the AST
/// in some way.  The Reducer is responsible for two tasks:
///
/// (1) Tracking the current lexical scope via enterX/exitX methods.
/// (2) Implementing reduceX methods to rewrite the AST.
///
/// Lexical scope consists of information such as the types of variables,
/// the current CFG, and basic block, etc.
///
/// The reduceX methods are responsible for rewriting terms.  After an SExpr
/// has been traversed, the traversal results are passed to reduceX(...), which
/// essentially implements an SExpr builder API.  A transform pass will
/// use this API to build a rewritten SExpr, but other passes may build
/// an object of some other type.  In functional programming terms, reduceX
/// implements a fold operation over the AST.
///
/// Because scope and rewriting are somewhat orthogonal, the traversal
/// infrastructure provides default implementations of the enter/exit methods,
/// and reduceX methods, in separate classes.  A reducer object should be
/// constructed via multiple inheritance.
///
template <class Self, class ReducerT>
class Traversal {
public:
  /// The underlying reducer interface, which implements TypeMap (for MAPTYPE).
  typedef ReducerT RedT;

  /// Cast this to the correct type (curiously recursive template pattern.)
  Self *self() { return static_cast<Self *>(this); }

  /// Invoked by SExpr classes to traverse data members.
  /// Do not override.
  MAPTYPE(RedT, SExpr) traverseDM(SExpr** Eptr, RedT *R) {
    SExpr *E = *Eptr;
    // Detect weak references to other instructions in the CFG.
    if (Instruction *I = E->asCFGInstruction())
      return R->handleResult(Eptr, R->reduceWeak(I));
    return R->handleResult(Eptr, self()->traverse(E, R, TRV_SubExpr));
  }

  /// Invoked by SExpr class to traverse owned data members.
  /// K should not be TRV_Weak; use traverseWeakDM instead.
  /// Possibly weak instructions should use traversDM(SExpr*, R).
  /// Do not override.
  template <class T>
  MAPTYPE(RedT, T) traverseDM(T** Eptr, RedT *R, TraversalKind K) {
    return R->handleResult(Eptr, self()->traverse(*Eptr, R, K));
  }

  /// Invoked by SExpr classes to traverse weak data members.
  /// Do not override.
  template <class T>
  MAPTYPE(RedT, T) traverseWeakDM(T** Eptr, RedT *R) {
    return R->handleResult(Eptr, R->reduceWeak(*Eptr));
  }

  /// Starting point for a traversal.
  /// Override this method to traverse SExprs of arbitrary type.
  template <class T>
  MAPTYPE(RedT, T)
  traverse(T* E, RedT *R, TraversalKind K) {
    if (!R->enterSubExpr(E, K))
      return R->skipTraverse(E);
    return R->exitSubExpr(E, traverseByType(E, R, K), K);
  }

  /// Override these methods to traverse a particular type of SExpr.
#define TIL_OPCODE_DEF(X)                                                 \
  MAPTYPE(RedT,X)                                                         \
  traverse##X(X *e, RedT *R, TraversalKind K) {                           \
    return e->traverse(*self(), R);                                       \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF


protected:
  /// For generic SExprs, do dynamic dispatch by type.
  MAPTYPE(RedT, SExpr)
  traverseByType(SExpr* E, RedT *R, TraversalKind K) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                    \
    case COP_##X:                                                            \
      return self()->traverse##X(cast<X>(E), R, K);
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return RedT::reduceNull();
  }

  /// For SExprs of known type, do static dispatch by type.
#define TIL_OPCODE_DEF(X)                                                    \
  MAPTYPE(RedT, X)                                                           \
  traverseByType(X* E, RedT *R, TraversalKind K) {              \
    return self()->traverse##X(E, R, K);                                     \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
};



// A ReadReducer simply returns each result.
template <class RedT>
class ReadReducer {
public:
  template <class T>
  MAPTYPE(RedT,T) handleResult(T** Eptr, MAPTYPE(RedT,T) Res) { return Res; }
};


// A WriteBackReducer will write traversal results back into the SExpr,
// thus performing an in-place destructive rewrite.
// RedT::TypeMap must be a type that can be assigned to an SExpr*.
template <class RedT>
class WriteBackReducer {
public:
  template <class T>
  MAPTYPE(RedT,T) handleResult(T** Eptr, MAPTYPE(RedT,T) Res) {
    *Eptr = Res;
    return Res;
  }
};



/// Implements default versions of the enter/exit routines for lexical scope.
/// for the reducer interface.
template <class RedT>
class DefaultScopeHandler {
public:
  /// Enter a new sub-expression E of traversal kind K.
  /// Return true to continue traversal, false to skip E.
  bool enterSubExpr(SExpr* E, TraversalKind K) { return true; }

  /// Handle cases where the traversal chooses to skip E.
  template <class T>
  MAPTYPE(RedT, T) skipTraverse(T *E) { return RedT::reduceNull(); }

  /// Finish traversing sub-expression E, with result Res, and return
  /// the result.
  template <class T>
  MAPTYPE(RedT,T) exitSubExpr(SExpr *E, MAPTYPE(RedT,T) Res, TraversalKind K) {
    return Res;
  }

  /// Enter the lexical scope of Orig, which is rewritten to Nvd.
  void enterScope(VarDecl* Orig, MAPTYPE(RedT, VarDecl) Nvd) { }

  /// Exit the lexical scope of Orig.
  void exitScope(VarDecl* Orig) { }

  /// Enter the basic block Orig, which will be rewritten to Nbb
  void enterBasicBlock(BasicBlock* Orig, MAPTYPE(RedT, BasicBlock) Nbb) { }

  /// Exit the basic block Orig.
  void exitBasicBlock(BasicBlock* Orig) { }

  /// Enter the SCFG Orig, which will be rewritten to Ns
  void enterCFG(SCFG* Orig, MAPTYPE(RedT, SCFG) Ns) { }

  /// Exit the lexical scope of Orig
  void exitCFG(SCFG* Orig) { }
};



/// Implements the reducer interface, with default versions for all methods.
/// R is a base class that defines the TypeMap.
template <class Self, class RedT>
class DefaultReducer {
public:
  typedef MAPTYPE(RedT, SExpr)       R_SExpr;
  typedef MAPTYPE(RedT, Instruction) R_Instruction;
  typedef MAPTYPE(RedT, Phi)         R_Phi;
  typedef MAPTYPE(RedT, VarDecl)     R_VarDecl;
  typedef MAPTYPE(RedT, BasicBlock)  R_BasicBlock;
  typedef MAPTYPE(RedT, SCFG)        R_SCFG;

  Self* self() { return static_cast<Self*>(this); }


  R_SExpr reduceSExpr(SExpr& Orig) {
    return RedT::reduceNull();
  }
  R_SExpr reduceInstruction(SExpr& Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceTerminator(Terminator& Orig) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reducePseudoTerm(SExpr& Orig) {
    return self()->reduceSExpr(Orig);
  }

  R_Instruction reduceWeak(Instruction* E) { return RedT::reduceNull(); }
  R_VarDecl     reduceWeak(VarDecl *E)     { return RedT::reduceNull(); }
  R_BasicBlock  reduceWeak(BasicBlock *E)  { return RedT::reduceNull(); }


  R_VarDecl reduceVarDecl(VarDecl &Orig, R_SExpr E) {
    return self()->reduceInstruction(Orig);
  }
  R_VarDecl reduceVarDeclLetrec(R_VarDecl VD, R_SExpr E) { return VD; }

  R_SExpr reduceFunction(Function &Orig, R_VarDecl Nvd, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceSFunction(SFunction &Orig, R_VarDecl Nvd, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceCode(Code &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceField(Field &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }

  R_SExpr reduceLiteral(Literal &Orig) {
    return self()->reduceInstruction(Orig);
  }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceLiteralPtr(LiteralPtr &Orig) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceVariable(Variable &Orig, R_VarDecl VD) {
    return self()->reduceInstruction(Orig);
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceProject(Project &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceCall(Call &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceAlloc(Alloc &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceLoad(Load &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceStore(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceArrayIndex(ArrayIndex &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceArrayAdd(ArrayAdd &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceUnaryOp(UnaryOp &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceBinaryOp(BinaryOp &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceCast(Cast &Orig, R_SExpr E0) {
    return self()->reduceInstruction(Orig);
  }


  R_Phi reducePhiBegin(Phi &Orig) {
    return self()->reduceInstruction(Orig);
  }
  void reducePhiArg(Phi &Orig, R_Phi Ph, unsigned i, R_SExpr E) { }
  R_Phi reducePhi(R_Phi Ph) { return Ph; }


  R_SCFG reduceSCFGBegin(SCFG &Orig) {
    return self()->reduceSExpr(Orig);
  }
  void reduceSCFGBlock(R_SCFG Scfg, unsigned i, R_BasicBlock B) { }
  R_SCFG reduceSCFG(R_SCFG Scfg) { return Scfg; }


  R_BasicBlock reduceBasicBlockBegin(BasicBlock &Orig) {
    return self()->reduceSExpr(Orig);
  }
  void reduceBasicBlockArg  (R_BasicBlock, unsigned i, R_SExpr E) { }
  void reduceBasicBlockInstr(R_BasicBlock, unsigned i, R_SExpr E) { }
  void reduceBasicBlockTerm (R_BasicBlock, R_SExpr E) { }
  R_BasicBlock reduceBasicBlock(R_BasicBlock BB) { return BB; }


  R_SExpr reduceGoto(Goto &Orig, R_BasicBlock B) {
    return self()->reduceTerminator(Orig);
  }
  R_SExpr reduceBranch(Branch &O, R_SExpr C, R_BasicBlock B0, R_BasicBlock B1) {
    return self()->reduceTerminator(O);
  }
  R_SExpr reduceReturn(Return &O, R_SExpr E) {
    return self()->reduceTerminator(O);
  }

  R_SExpr reduceUndefined(Undefined &Orig) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceWildcard(Wildcard &Orig) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceIdentifier(Identifier &Orig) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceLet(Let &Orig, R_VarDecl Nvd, R_SExpr B) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceLetrec(Letrec &Orig, R_VarDecl Nvd, R_SExpr B) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return self()->reducePseudoTerm(Orig);
  }
};


/// Builds a ReadReducer with default versions of all methods.
template <class Self, class RedT>
class DefaultReadReducer : public RedT,
                           public ReadReducer<RedT>,
                           public DefaultScopeHandler<RedT>,
                           public DefaultReducer<Self, RedT> { };


/// Defines the TypeMap for VisitReducerBase
class VisitReducerMap {
public:
  /// A visitor maps all expression types to bool.
  template <class T> struct TypeMap { typedef bool Ty; };
  typedef bool NullType;

  static bool reduceNull() { return true; }
};


/// Implements reduceX methods for a simple visitor.   A visitor "rewrites"
/// SExprs to booleans: it returns true on success, and false on failure.
template<class Self>
class VisitReducer : public DefaultReadReducer<Self, VisitReducerMap> {
public:
  VisitReducer() : Success(true) { }

  bool reduceSExpr(SExpr &Orig) { return true; }

  bool enterSubExpr(SExpr* E, TraversalKind K) {
    return Success;  // Abort on failure.
  }

  bool exitSubExpr(SExpr *E, bool Res, TraversalKind K) {
    Success = Success && Res;
    return Success;
  }

  bool skipTraverse(SExpr* E) { return false; }

  class VisitTraversal : public Traversal<VisitTraversal, Self> { };

  static bool visit(SExpr *E) {
    VisitTraversal Traverser;
    Self           Reducer;
    return Traverser.traverse(E, &Reducer, TRV_Tail);
  }

private:
  bool Success;
};



// Used by SExprReducerMap.  Most terms map to SExpr*.
template <class T> struct SExprTypeMap { typedef SExpr* Ty; };

// These kinds of SExpr must map to the same kind.
// We define these here b/c template specializations cannot be class members.
template<> struct SExprTypeMap<VarDecl>    { typedef VarDecl* Ty; };
template<> struct SExprTypeMap<BasicBlock> { typedef BasicBlock* Ty; };

/// Defines the TypeMap for travesals that return SExprs.
class SExprReducerMap {
public:
  // An SExpr reducer rewrites one SExpr to another.
  template <class T> struct TypeMap : public SExprTypeMap<T> { };

  typedef std::nullptr_t NullType;

  static NullType reduceNull() { return nullptr; }
};



////////////////////////////////////////
// traverse methods for all TIL classes.
////////////////////////////////////////

template <class V>
MAPTYPE(V::RedT, VarDecl) VarDecl::traverse(V &Vs, typename V::RedT *R) {
  switch (kind()) {
    case VK_Fun: {
      auto D = Vs.traverseDM(&Definition, R, TRV_Type);
      return R->reduceVarDecl(*this, D);
    }
    case VK_SFun: {
      // Don't traverse the definition, since it cyclicly points back to self.
      // Just create a new (dummy) definition.
      return R->reduceVarDecl(*this, V::RedT::reduceNull());
    }
    case VK_Let: {
      auto D = Vs.traverseDM(&Definition, R, TRV_SubExpr);
      return R->reduceVarDecl(*this, D);
    }
    case VK_Letrec: {
      // Create a new (empty) definition.
      auto Nvd = R->reduceVarDecl(*this, V::RedT::reduceNull());
      // Enter the scope of the empty definition.
      R->enterScope(this, Nvd);
      // Traverse the definition, and hope recursive references are lazy.
      auto D = Vs.traverseDM(&Definition, R, TRV_SubExpr);
      R->exitScope(this);
      return R->reduceVarDeclLetrec(Nvd, D);
    }
  }
}

template <class V>
MAPTYPE(V::RedT, Function) Function::traverse(V &Vs, typename V::RedT *R) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverseDM(&VDecl, R, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the function.
  R->enterScope(VDecl, E0);
  auto E1 = Vs.traverseDM(&Body, R, TRV_SubExpr);
  R->exitScope(VDecl);
  return R->reduceFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, SFunction) SFunction::traverse(V &Vs, typename V::RedT *R) {
  // Traversing an self-definition is a no-op.
  auto E0 = Vs.traverseDM(&VDecl, R, TRV_SubExpr);
  R->enterScope(VDecl, E0);
  auto E1 = Vs.traverseDM(&Body, R, TRV_SubExpr);
  R->exitScope(VDecl);
  // The SFun constructor will set E0->Definition to E1.
  return R->reduceSFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, Code) Code::traverse(V &Vs, typename V::RedT *R) {
  auto Nt = Vs.traverseDM(&ReturnType, R, TRV_Type);
  auto Nb = Vs.traverseDM(&Body,       R, TRV_Lazy);
  return R->reduceCode(*this, Nt, Nb);
}

template <class V>
MAPTYPE(V::RedT, Field) Field::traverse(V &Vs, typename V::RedT *R) {
  auto Nr = Vs.traverseDM(&Range, R, TRV_Type);
  auto Nb = Vs.traverseDM(&Body,  R, TRV_Lazy);
  return R->reduceField(*this, Nr, Nb);
}


template <class V>
MAPTYPE(V::RedT, Literal) Literal::traverse(V &Vs, typename V::RedT *R) {
  switch (ValType.Base) {
  case ValueType::BT_Void:
    break;
  case ValueType::BT_Bool:
    return R->reduceLiteralT(as<bool>());
  case ValueType::BT_Int: {
    switch (ValType.Size) {
    case ValueType::ST_8:
      if (ValType.Signed)
        return R->reduceLiteralT(as<int8_t>());
      else
        return R->reduceLiteralT(as<uint8_t>());
    case ValueType::ST_16:
      if (ValType.Signed)
        return R->reduceLiteralT(as<int16_t>());
      else
        return R->reduceLiteralT(as<uint16_t>());
    case ValueType::ST_32:
      if (ValType.Signed)
        return R->reduceLiteralT(as<int32_t>());
      else
        return R->reduceLiteralT(as<uint32_t>());
    case ValueType::ST_64:
      if (ValType.Signed)
        return R->reduceLiteralT(as<int64_t>());
      else
        return R->reduceLiteralT(as<uint64_t>());
    default:
      break;
    }
    break;
  }
  case ValueType::BT_Float: {
    switch (ValType.Size) {
    case ValueType::ST_32:
      return R->reduceLiteralT(as<float>());
    case ValueType::ST_64:
      return R->reduceLiteralT(as<double>());
    default:
      break;
    }
    break;
  }
  case ValueType::BT_String:
    return R->reduceLiteralT(as<StringRef>());
  case ValueType::BT_Pointer:
    return R->reduceLiteralT(as<void*>());
  case ValueType::BT_ValueRef:
    break;
  }
  return R->reduceLiteral(*this);
}

template <class V>
MAPTYPE(V::RedT, LiteralPtr) LiteralPtr::traverse(V &Vs, typename V::RedT *R) {
  return R->reduceLiteralPtr(*this);
}

template<class V>
MAPTYPE(V::RedT, Variable) Variable::traverse(V &Vs, typename V::RedT *R) {
  return R->reduceVariable(*this, Vs.traverseWeakDM(&VDecl, R));
}

template <class V>
MAPTYPE(V::RedT, Apply) Apply::traverse(V &Vs, typename V::RedT *R) {
  auto Nf = Vs.traverseDM(&Fun, R, TRV_Path);
  auto Na = Vs.traverseDM(&Arg, R);
  return R->reduceApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, SApply) SApply::traverse(V &Vs, typename V::RedT *R) {
  auto Nf = Vs.traverseDM(&Sfun, R, TRV_Path);
  auto Na = Arg ? Vs.traverseDM(&Arg, R) : V::RedT::reduceNull();
  return R->reduceSApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, Project) Project::traverse(V &Vs, typename V::RedT *R) {
  auto Nr = Vs.traverseDM(&Rec, R, TRV_Path);
  return R->reduceProject(*this, Nr);
}

template <class V>
MAPTYPE(V::RedT, Call) Call::traverse(V &Vs, typename V::RedT *R) {
  auto Nt = Vs.traverseDM(&Target, R, TRV_Path);
  return R->reduceCall(*this, Nt);
}

template <class V>
MAPTYPE(V::RedT, Alloc) Alloc::traverse(V &Vs, typename V::RedT *R) {
  auto Nd = Vs.traverseDM(&Dtype, R, TRV_SubExpr);
  return R->reduceAlloc(*this, Nd);
}

template <class V>
MAPTYPE(V::RedT, Load) Load::traverse(V &Vs, typename V::RedT *R) {
  auto Np = Vs.traverseDM(&Ptr, R);
  return R->reduceLoad(*this, Np);
}

template <class V>
MAPTYPE(V::RedT, Store) Store::traverse(V &Vs, typename V::RedT *R) {
  auto Np = Vs.traverseDM(&Dest,   R);
  auto Nv = Vs.traverseDM(&Source, R);
  return R->reduceStore(*this, Np, Nv);
}

template <class V>
MAPTYPE(V::RedT, ArrayIndex) ArrayIndex::traverse(V &Vs, typename V::RedT *R) {
  auto Na = Vs.traverseDM(&Array, R);
  auto Ni = Vs.traverseDM(&Index, R);
  return R->reduceArrayIndex(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, ArrayAdd) ArrayAdd::traverse(V &Vs, typename V::RedT *R) {
  auto Na = Vs.traverseDM(&Array, R);
  auto Ni = Vs.traverseDM(&Index, R);
  return R->reduceArrayAdd(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, UnaryOp) UnaryOp::traverse(V &Vs, typename V::RedT *R) {
  auto Ne = Vs.traverseDM(&Expr0, R);
  return R->reduceUnaryOp(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BinaryOp) BinaryOp::traverse(V &Vs, typename V::RedT *R) {
  auto Ne0 = Vs.traverseDM(&Expr0, R);
  auto Ne1 = Vs.traverseDM(&Expr1, R);
  return R->reduceBinaryOp(*this, Ne0, Ne1);
}

template <class V>
MAPTYPE(V::RedT, Cast) Cast::traverse(V &Vs, typename V::RedT *R) {
  auto Ne = Vs.traverseDM(&Expr0, R);
  return R->reduceCast(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, Phi) Phi::traverse(V &Vs, typename V::RedT *R) {
  auto Np = R->reducePhiBegin(*this);
  unsigned i = 0;
  for (auto *Va : Values) {
    R->reducePhiArg(*this, Np, i, Vs.traverseDM(&Va, R));
    ++i;
  }
  return R->reducePhi(Np);
}

template <class V>
MAPTYPE(V::RedT, Goto) Goto::traverse(V &Vs, typename V::RedT *R) {
  auto Ntb = Vs.traverseWeakDM(&TargetBlock, R);
  return R->reduceGoto(*this, Ntb);
}

template <class V>
MAPTYPE(V::RedT, Branch) Branch::traverse(V &Vs, typename V::RedT *R) {
  auto Nc  = Vs.traverseDM(&Condition, R);
  auto Ntb = Vs.traverseWeakDM(&Branches[0], R);
  auto Nte = Vs.traverseWeakDM(&Branches[1], R);
  return R->reduceBranch(*this, Nc, Ntb, Nte);
}

template <class V>
MAPTYPE(V::RedT, Return) Return::traverse(V &Vs, typename V::RedT *R) {
  auto Ne = Vs.traverseDM(&Retval, R);
  return R->reduceReturn(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BasicBlock) BasicBlock::traverse(V &Vs, typename V::RedT *R) {
  auto Nb = R->reduceBasicBlockBegin(*this);
  R->enterBasicBlock(this, Nb);
  unsigned i = 0;
  for (Phi* &A : Args) {
    // Use TRV_SubExpr to force traversal of arguments
    R->reduceBasicBlockArg(Nb, i, Vs.traverseDM(&A, R, TRV_SubExpr));
    ++i;
  }
  i = 0;
  for (Instruction* &I : Instrs) {
    // Use TRV_SubExpr to force traversal of instructions
    R->reduceBasicBlockInstr(Nb, i, Vs.traverseDM(&I, R, TRV_SubExpr));
    ++i;
  }
  R->exitBasicBlock(this);
  R->reduceBasicBlockTerm(Nb, Vs.traverseDM(&TermInstr, R, TRV_SubExpr));

  return R->reduceBasicBlock(Nb);
}

template <class V>
MAPTYPE(V::RedT, SCFG) SCFG::traverse(V &Vs, typename V::RedT *R) {
  auto Ns = R->reduceSCFGBegin(*this);
  R->enterCFG(this, Ns);

  unsigned i = 0;
  for (BasicBlock *&B : Blocks) {
    auto Nb = Vs.traverseDM(&B, R, TRV_SubExpr);
    R->reduceSCFGBlock(Ns, i, Nb);
    ++i;
  }
  R->exitCFG(this);
  return R->reduceSCFG(Ns);
}

template <class V>
MAPTYPE(V::RedT, Future) Future::traverse(V &Vs, typename V::RedT *R) {
  assert(Result && "Cannot traverse Future that has not been forced.");
  return Vs.traverseDM(&Result, R);
}

template <class V>
MAPTYPE(V::RedT, Undefined) Undefined::traverse(V &Vs, typename V::RedT *R) {
  return R->reduceUndefined(*this);
}

template <class V>
MAPTYPE(V::RedT, Wildcard) Wildcard::traverse(V &Vs, typename V::RedT *R) {
  return R->reduceWildcard(*this);
}

template <class V>
MAPTYPE(V::RedT, Identifier)
Identifier::traverse(V &Vs, typename V::RedT *R) {
  return R->reduceIdentifier(*this);
}

template <class V>
MAPTYPE(V::RedT, Let) Let::traverse(V &Vs, typename V::RedT *R) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverseDM(&VDecl, R, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the let variable.
  R->enterScope(VDecl, E0);
  auto E1 = Vs.traverseDM(&Body, R, TRV_Tail);
  R->exitScope(VDecl);
  return R->reduceLet(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, Letrec) Letrec::traverse(V &Vs, typename V::RedT *R) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverseDM(&VDecl, R, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the let variable.
  R->enterScope(VDecl, E0);
  auto E1 = Vs.traverseDM(&Body, R, TRV_Tail);
  R->exitScope(VDecl);
  return R->reduceLetrec(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, IfThenElse) IfThenElse::traverse(V &Vs, typename V::RedT *R) {
  auto Nc = Vs.traverseDM(&Condition, R);
  auto Nt = Vs.traverseDM(&ThenExpr,  R, TRV_Tail);
  auto Ne = Vs.traverseDM(&ElseExpr,  R, TRV_Tail);
  return R->reduceIfThenElse(*this, Nc, Nt, Ne);
}


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


///////////////////////////////////////////
// Implement compare for all TIL classes.
///////////////////////////////////////////

template <class C>
typename C::CType VarDecl::compare(const VarDecl* E, C& Cmp) const {
  auto Ct = Cmp.compareIntegers(kind(), E->kind());
  if (Cmp.notTrue(Ct))
    return Ct;
  // Note, we don't compare names, due to alpha-renaming.
  return Cmp.compare(Definition, E->Definition);
}

template <class C>
typename C::CType Function::compare(const Function* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compare(VDecl->definition(), E->VDecl->definition());
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.leaveScope();
  return Ct;
}

template <class C>
typename C::CType SFunction::compare(const SFunction* E, C& Cmp) const {
  Cmp.enterScope(variableDecl(), E->variableDecl());
  typename C::CType Ct = Cmp.compare(body(), E->body());
  Cmp.leaveScope();
  return Ct;
}

template <class C>
typename C::CType Code::compare(const Code* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(returnType(), E->returnType());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(body(), E->body());
}

template <class C>
typename C::CType Field::compare(const Field* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(range(), E->range());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(body(), E->body());
}

template <class C>
typename C::CType Literal::compare(const Literal* E, C& Cmp) const {
  // TODO: defer actual comparison to LiteralT
  return Cmp.trueResult();
}

template <class C>
typename C::CType LiteralPtr::compare(const LiteralPtr* E, C& Cmp) const {
  return Cmp.comparePointers(Cvdecl, E->Cvdecl);
}

template <class C>
typename C::CType Variable::compare(const Variable* E, C& Cmp) const {
  // TODO: compare weak refs.
  return Cmp.comparePointers(VDecl, E->VDecl);
}

template <class C>
typename C::CType Apply::compare(const Apply* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(fun(), E->fun());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(arg(), E->arg());
}

template <class C>
typename C::CType SApply::compare(const SApply* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(sfun(), E->sfun());
  if (Cmp.notTrue(Ct) || (!arg() && !E->arg()))
    return Ct;
  return Cmp.compare(arg(), E->arg());
}

template <class C>
typename C::CType Project::compare(const Project* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(record(), E->record());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.comparePointers(Cvdecl, E->Cvdecl);
}

template <class C>
typename C::CType Call::compare(const Call* E, C& Cmp) const {
  return Cmp.compare(target(), E->target());
}


template <class C>
typename C::CType Alloc::compare(const Alloc* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compareIntegers(kind(), E->kind());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(dataType(), E->dataType());
}

template <class C>
typename C::CType Load::compare(const Load* E, C& Cmp) const {
  return Cmp.compare(pointer(), E->pointer());
}

template <class C>
typename C::CType Store::compare(const Store* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(destination(), E->destination());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(source(), E->source());
}

template <class C>
typename C::CType ArrayIndex::compare(const ArrayIndex* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(array(), E->array());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(index(), E->index());
}

template <class C>
typename C::CType ArrayAdd::compare(const ArrayAdd* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(array(), E->array());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(index(), E->index());
}

template <class C>
typename C::CType UnaryOp::compare(const UnaryOp* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(unaryOpcode(), E->unaryOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr(), E->expr());
}

template <class C>
typename C::CType BinaryOp::compare(const BinaryOp* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(binaryOpcode(), E->binaryOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  Ct = Cmp.compare(expr0(), E->expr0());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr1(), E->expr1());
}

template <class C>
typename C::CType Cast::compare(const Cast* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(castOpcode(), E->castOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr(), E->expr());
}

template <class C>
typename C::CType Phi::compare(const Phi *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Goto::compare(const Goto *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Branch::compare(const Branch *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Return::compare(const Return *E, C &Cmp) const {
  return Cmp.compare(Retval, E->Retval);
}

template <class C>
typename C::CType BasicBlock::compare(const BasicBlock *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType SCFG::compare(const SCFG *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Future::compare(const Future* E, C& Cmp) const {
  if (!Result || !E->Result)
    return Cmp.comparePointers(this, E);
  return Cmp.compare(Result, E->Result);
}

template <class C>
typename C::CType Undefined::compare(const Undefined* E, C& Cmp) const {
  return Cmp.trueResult();
}

template <class C>
typename C::CType Wildcard::compare(const Wildcard* E, C& Cmp) const {
  return Cmp.trueResult();
}

template <class C>
typename C::CType Identifier::compare(const Identifier* E, C& Cmp) const {
  return Cmp.compareStrings(name(), E->name());
}

template <class C>
typename C::CType Let::compare(const Let* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(VDecl, E->VDecl);
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.leaveScope();
  return Ct;
}

template <class C>
typename C::CType Letrec::compare(const Letrec* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(variableDecl(), E->variableDecl());
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.leaveScope();
  return Ct;
}

template <class C>
typename C::CType IfThenElse::compare(const IfThenElse* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(condition(), E->condition());
  if (Cmp.notTrue(Ct))
    return Ct;
  Ct = Cmp.compare(thenExpr(), E->thenExpr());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(elseExpr(), E->elseExpr());
}



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
