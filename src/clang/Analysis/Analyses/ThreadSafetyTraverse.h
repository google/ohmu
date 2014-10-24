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
///   * traverseX(...) is the entry point for a taversal of node X.
///
/// This class must be combined with other classes to implement the full
/// traversal interface:
///
///   * A Reducer class, e.g. VisitReducer, CopyReducer, or InplaceReducer.
///   * A ScopeHandler (DefaultScopeHandler) to handle lexical scope.
///
/// The Reducer class implements reduceX methods, which are responsible for
/// rewriting terms.  After an SExpr has been traversed, the traversal results
/// are passed to reduceX(...), which essentially implements an SExpr builder
/// API.  A transform pass will use this API to build a rewritten SExpr, but
/// other passes may build an object of some other type.  In functional
/// programming terms, reduceX implements a fold operation over the AST.
///
/// ReducerMapType is a trait class for the Reducer, which defines a TypeMap
/// function that describes what type of results the reducer will produce.
/// A rewrite pass will rewrite SExpr -> SExpr, while a visitor pass will just
/// return a bool.
///
/// Reducers must also implement handleResult(...).  Destructive rewrite
/// passes can use this method to modify SExprs in place.
///
template <class Self, class ReducerMapType>
class Traversal {
public:
  /// The underlying reducer interface, which implements TypeMap (for MAPTYPE).
  typedef ReducerMapType RMap;

  /// Cast this to the correct type (curiously recursive template pattern.)
  Self *self() { return static_cast<Self *>(this); }

  /// Initial starting point, to be called by external routines.
  MAPTYPE(RMap, SExpr) traverseAll(SExpr* E) {
    return self()->traverse(E, TRV_Tail);
  }

  /// Invoked by SExpr classes to traverse writable data members.
  /// Do not override.
  MAPTYPE(RMap, SExpr) handleTraverse(SExpr** Eptr) {
    return self()->handleResult(Eptr, self()->traverseArg(*Eptr));
  }

  /// Invoked by SExpr class to traverse owned writable data members.
  /// Do not override.
  template<class T>
  MAPTYPE(RMap, T) handleTraverse(T** Eptr, TraversalKind K) {
    return self()->handleResult(Eptr, self()->traverse(*Eptr, K));
  }

  /// Invoked by SExpr classes to traverse weak data members.
  /// Do not override.
  template <class T>
  MAPTYPE(RMap, T) handleTraverseWeak(T** Eptr) {
    return self()->handleResult(Eptr, self()->reduceWeak(*Eptr));
  }

  /// Invoked by SExpr classes to traverse possibly weak members.
  /// Do not override.
  MAPTYPE(RMap, SExpr) traverseArg(SExpr* E) {
    // Detect weak references to other instructions in the CFG.
    if (Instruction *I = E->asCFGInstruction())
      return self()->reduceWeak(I);
    return self()->traverse(E, TRV_SubExpr);
  }

  /// Starting point for a traversal.
  /// Override this method to traverse SExprs of arbitrary type.
  template <class T>
  MAPTYPE(RMap, T) traverse(T* E, TraversalKind K) {
    return traverseByType(E, K);
  }

  /// Override these methods to traverse a particular type of SExpr.
#define TIL_OPCODE_DEF(X)                                                 \
  MAPTYPE(RMap,X) traverse##X(X *e, TraversalKind K) {                    \
    return e->traverse(*self());                                          \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF


protected:
  /// For generic SExprs, do dynamic dispatch by type.
  MAPTYPE(RMap, SExpr) traverseByType(SExpr* E, TraversalKind K) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                 \
    case COP_##X:                                                         \
      return self()->traverse##X(cast<X>(E), K);
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return RMap::reduceNull();
  }

  /// For SExprs of known type, do static dispatch by type.
#define TIL_OPCODE_DEF(X)                                                 \
  MAPTYPE(RMap, X) traverseByType(X* E, TraversalKind K) {                \
    return self()->traverse##X(E, K);                                     \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
};



/// Implements default versions of the enter/exit routines for lexical scope.
/// for the reducer interface.
template <class RMap>
class DefaultScopeHandler {
public:
  /// Enter the lexical scope of Orig, which is rewritten to Nvd.
  void enterScope(VarDecl* Orig, MAPTYPE(RMap, VarDecl) Nvd) { }

  /// Exit the lexical scope of Orig.
  void exitScope(VarDecl* Orig) { }
};



/// Implements the reducer interface, with default versions for all methods.
/// R is a base class that defines the TypeMap.
template <class Self, class RMap>
class DefaultReducer {
public:
  typedef MAPTYPE(RMap, SExpr)       R_SExpr;
  typedef MAPTYPE(RMap, VarDecl)     R_VarDecl;
  typedef MAPTYPE(RMap, Slot)        R_Slot;
  typedef MAPTYPE(RMap, Record)      R_Record;
  typedef MAPTYPE(RMap, Goto)        R_Goto;
  typedef MAPTYPE(RMap, Phi)         R_Phi;
  typedef MAPTYPE(RMap, BasicBlock)  R_BasicBlock;
  typedef MAPTYPE(RMap, SCFG)        R_SCFG;

  Self* self() { return static_cast<Self*>(this); }

  /// Default implementation is just to return the result.
  template <class T>
  MAPTYPE(RMap,T) handleResult(T** Eptr, MAPTYPE(RMap,T) Res) { return Res; }

  void handleRecordSlot(R_Record E, R_Slot Res) { }
  void handlePhiArg  (Phi &Orig, R_Goto NG, R_SExpr Res) { }
  void handleBBArg   (Phi &Orig,         R_SExpr Res)      { }
  void handleBBInstr (Instruction &Orig, R_SExpr Res)      { }
  void handleCFGBlock(BasicBlock &Orig,  R_BasicBlock Res) { }

  R_SExpr reduceSExpr(SExpr& Orig) { return RMap::reduceNull(); }

  R_SExpr      reduceWeak(Instruction* E) { return RMap::reduceNull(); }
  R_VarDecl    reduceWeak(VarDecl *E)     { return RMap::reduceNull(); }
  R_BasicBlock reduceWeak(BasicBlock *E)  { return RMap::reduceNull(); }

  R_VarDecl reduceVarDecl(VarDecl &Orig, R_SExpr E) {
    return self()->reduceSExpr(Orig);
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
  R_Slot reduceSlot(Slot &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_Record reduceRecordBegin(Record &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_Record reduceRecordEnd(R_Record R) { return R; }


  R_SExpr reduceLiteral(Literal &Orig) {
    return self()->reduceSExpr(Orig);
  }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceLiteralPtr(LiteralPtr &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceVariable(Variable &Orig, R_VarDecl VD) {
    return self()->reduceSExpr(Orig);
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceProject(Project &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceCall(Call &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceAlloc(Alloc &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceLoad(Load &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceStore(Store &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceArrayIndex(ArrayIndex &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceArrayAdd(ArrayAdd &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceUnaryOp(UnaryOp &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceBinaryOp(BinaryOp &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceCast(Cast &Orig, R_SExpr E0) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceBranch(Branch &O, R_SExpr C, R_BasicBlock B0, R_BasicBlock B1) {
    return self()->reduceSExpr(O);
  }
  R_SExpr reduceReturn(Return &O, R_SExpr E) {
    return self()->reduceSExpr(O);
  }
  R_SExpr reduceGotoBegin(Goto &Orig, R_BasicBlock B) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceGotoEnd(R_Goto G) { return G; }

  R_Phi reducePhi(Phi &Orig) {
    return self()->reduceSExpr(Orig);
  }

  R_BasicBlock reduceBasicBlockBegin(BasicBlock &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_BasicBlock reduceBasicBlockEnd(R_BasicBlock BB, R_SExpr Tm) { return BB; }

  R_SCFG reduceSCFG_Begin(SCFG &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SCFG reduceSCFG_End(R_SCFG Scfg) { return Scfg; }

  R_SExpr reduceUndefined(Undefined &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceWildcard(Wildcard &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceIdentifier(Identifier &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceLet(Let &Orig, R_VarDecl Nvd, R_SExpr B) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceLetrec(Letrec &Orig, R_VarDecl Nvd, R_SExpr B) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return self()->reduceSExpr(Orig);
  }
};


/// Defines the TypeMap for VisitReducer
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
class VisitReducer : public Traversal<Self, VisitReducerMap>,
                     public DefaultReducer<Self, VisitReducerMap>,
                     public DefaultScopeHandler<VisitReducerMap> {
public:
  typedef Traversal<Self, VisitReducerMap> SuperTv;

  VisitReducer() : Success(true) { }

  bool reduceSExpr(SExpr &Orig) { return true; }

  /// Abort traversal on failure.
  template <class T>
  MAPTYPE(VisitReducerMap, T) traverse(T* E, TraversalKind K) {
    Success = Success && SuperTv::traverse(E, K);
    return Success;
  }

  static bool visit(SExpr *E) {
    Self Visitor;
    return Visitor.traverse(E, TRV_Tail);
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
template<> struct SExprTypeMap<Slot>       { typedef Slot* Ty; };

/// Defines the TypeMap for traversals that return SExprs.
/// See CopyReducer and InplaceReducer for details.
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
MAPTYPE(V::RMap, VarDecl) VarDecl::traverse(V &Vs) {
  switch (kind()) {
    case VK_Fun: {
      auto D = Vs.handleTraverse(&Definition, TRV_Type);
      return Vs.reduceVarDecl(*this, D);
    }
    case VK_SFun: {
      // Don't traverse the definition, since it cyclicly points back to self.
      // Just create a new (dummy) definition.
      return Vs.reduceVarDecl(*this, V::RMap::reduceNull());
    }
    case VK_Let: {
      auto D = Vs.handleTraverse(&Definition, TRV_SubExpr);
      return Vs.reduceVarDecl(*this, D);
    }
    case VK_Letrec: {
      // Create a new (empty) definition.
      auto Nvd = Vs.reduceVarDecl(*this, V::RMap::reduceNull());
      // Enter the scope of the empty definition.
      Vs.enterScope(this, Nvd);
      // Traverse the definition, and hope recursive references are lazy.
      auto D = Vs.handleTraverse(&Definition, TRV_SubExpr);
      Vs.exitScope(this);
      return Vs.reduceVarDeclLetrec(Nvd, D);
    }
  }
}

template <class V>
MAPTYPE(V::RMap, Function) Function::traverse(V &Vs) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.handleTraverse(&VDecl, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the function.
  Vs.enterScope(VDecl, E0);
  auto E1 = Vs.handleTraverse(&Body, TRV_SubExpr);
  Vs.exitScope(VDecl);
  return Vs.reduceFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, SFunction) SFunction::traverse(V &Vs) {
  // Traversing an self-definition is a no-op.
  auto E0 = Vs.handleTraverse(&VDecl, TRV_SubExpr);
  Vs.enterScope(VDecl, E0);
  auto E1 = Vs.handleTraverse(&Body, TRV_SubExpr);
  Vs.exitScope(VDecl);
  // The SFun constructor will set E0->Definition to E1.
  return Vs.reduceSFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, Code) Code::traverse(V &Vs) {
  auto Nt = Vs.handleTraverse(&ReturnType, TRV_Type);
  auto Nb = Vs.handleTraverse(&Body, TRV_Lazy);
  return Vs.reduceCode(*this, Nt, Nb);
}

template <class V>
MAPTYPE(V::RMap, Field) Field::traverse(V &Vs) {
  auto Nr = Vs.handleTraverse(&Range, TRV_Type);
  auto Nb = Vs.handleTraverse(&Body, TRV_Lazy);
  return Vs.reduceField(*this, Nr, Nb);
}

template <class V>
MAPTYPE(V::RMap, Slot) Slot::traverse(V &Vs) {
  auto Nd = Vs.handleTraverse(&Definition, TRV_SubExpr);
  return Vs.reduceSlot(*this, Nd);
}

template <class V>
MAPTYPE(V::RMap, Record) Record::traverse(V &Vs) {
  auto Nr = Vs.reduceRecordBegin(*this);
  for (auto *&Slt : Slots) {
    Vs.handleRecordSlot(Nr, Vs.traverse(Slt, TRV_SubExpr));
  }
  return Vs.reduceRecordEnd(Nr);
}


template <class V>
MAPTYPE(V::RMap, Literal) Literal::traverse(V &Vs) {
  switch (ValType.Base) {
  case ValueType::BT_Void:
    break;
  case ValueType::BT_Bool:
    return Vs.reduceLiteralT(as<bool>());
  case ValueType::BT_Int: {
    switch (ValType.Size) {
    case ValueType::ST_8:
      if (ValType.Signed)
        return Vs.reduceLiteralT(as<int8_t>());
      else
        return Vs.reduceLiteralT(as<uint8_t>());
    case ValueType::ST_16:
      if (ValType.Signed)
        return Vs.reduceLiteralT(as<int16_t>());
      else
        return Vs.reduceLiteralT(as<uint16_t>());
    case ValueType::ST_32:
      if (ValType.Signed)
        return Vs.reduceLiteralT(as<int32_t>());
      else
        return Vs.reduceLiteralT(as<uint32_t>());
    case ValueType::ST_64:
      if (ValType.Signed)
        return Vs.reduceLiteralT(as<int64_t>());
      else
        return Vs.reduceLiteralT(as<uint64_t>());
    default:
      break;
    }
    break;
  }
  case ValueType::BT_Float: {
    switch (ValType.Size) {
    case ValueType::ST_32:
      return Vs.reduceLiteralT(as<float>());
    case ValueType::ST_64:
      return Vs.reduceLiteralT(as<double>());
    default:
      break;
    }
    break;
  }
  case ValueType::BT_String:
    return Vs.reduceLiteralT(as<StringRef>());
  case ValueType::BT_Pointer:
    return Vs.reduceLiteralT(as<void*>());
  case ValueType::BT_ValueRef:
    break;
  }
  return Vs.reduceLiteral(*this);
}

template <class V>
MAPTYPE(V::RMap, LiteralPtr) LiteralPtr::traverse(V &Vs) {
  return Vs.reduceLiteralPtr(*this);
}

template<class V>
MAPTYPE(V::RMap, Variable) Variable::traverse(V &Vs) {
  return Vs.reduceVariable(*this, Vs.handleTraverseWeak(&VDecl));
}

template <class V>
MAPTYPE(V::RMap, Apply) Apply::traverse(V &Vs) {
  auto Nf = Vs.handleTraverse(&Fun, TRV_Path);
  auto Na = Vs.handleTraverse(&Arg);
  return Vs.reduceApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RMap, SApply) SApply::traverse(V &Vs) {
  auto Nf = Vs.handleTraverse(&Sfun, TRV_Path);
  auto Na = Arg ? Vs.handleTraverse(&Arg) : V::RMap::reduceNull();
  return Vs.reduceSApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RMap, Project) Project::traverse(V &Vs) {
  auto Nr = Vs.handleTraverse(&Rec, TRV_Path);
  return Vs.reduceProject(*this, Nr);
}

template <class V>
MAPTYPE(V::RMap, Call) Call::traverse(V &Vs) {
  auto Nt = Vs.handleTraverse(&Target, TRV_Path);
  return Vs.reduceCall(*this, Nt);
}

template <class V>
MAPTYPE(V::RMap, Alloc) Alloc::traverse(V &Vs) {
  auto Nd = Vs.handleTraverse(&InitExpr);
  return Vs.reduceAlloc(*this, Nd);
}

template <class V>
MAPTYPE(V::RMap, Load) Load::traverse(V &Vs) {
  auto Np = Vs.handleTraverse(&Ptr);
  return Vs.reduceLoad(*this, Np);
}

template <class V>
MAPTYPE(V::RMap, Store) Store::traverse(V &Vs) {
  auto Np = Vs.handleTraverse(&Dest);
  auto Nv = Vs.handleTraverse(&Source);
  return Vs.reduceStore(*this, Np, Nv);
}

template <class V>
MAPTYPE(V::RMap, ArrayIndex) ArrayIndex::traverse(V &Vs) {
  auto Na = Vs.handleTraverse(&Array);
  auto Ni = Vs.handleTraverse(&Index);
  return Vs.reduceArrayIndex(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RMap, ArrayAdd) ArrayAdd::traverse(V &Vs) {
  auto Na = Vs.handleTraverse(&Array);
  auto Ni = Vs.handleTraverse(&Index);
  return Vs.reduceArrayAdd(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RMap, UnaryOp) UnaryOp::traverse(V &Vs) {
  auto Ne = Vs.handleTraverse(&Expr0);
  return Vs.reduceUnaryOp(*this, Ne);
}

template <class V>
MAPTYPE(V::RMap, BinaryOp) BinaryOp::traverse(V &Vs) {
  auto Ne0 = Vs.handleTraverse(&Expr0);
  auto Ne1 = Vs.handleTraverse(&Expr1);
  return Vs.reduceBinaryOp(*this, Ne0, Ne1);
}

template <class V>
MAPTYPE(V::RMap, Cast) Cast::traverse(V &Vs) {
  auto Ne = Vs.handleTraverse(&Expr0);
  return Vs.reduceCast(*this, Ne);
}

template <class V>
MAPTYPE(V::RMap, Phi) Phi::traverse(V &Vs) {
  // Note: traversing a Phi does not traverse it's arguments.
  // The arguments are traversed by the Goto.
  return Vs.reducePhi(*this);
}

template <class V>
MAPTYPE(V::RMap, Goto) Goto::traverse(V &Vs) {
  auto Ntb = Vs.handleTraverseWeak(&TargetBlock);
  auto Ng  = Vs.reduceGotoBegin(*this, Ntb);
  for (Phi *Ph : TargetBlock->arguments()) {
    if (Ph && Ph->instrID() > 0) {
      // Ignore any newly-added Phi nodes (e.g. from an in-place SSA pass.)
      Vs.handlePhiArg(*Ph, Ng, Vs.traverseArg(Ph->values()[phiIndex()]));
    }
  }
  return Vs.reduceGotoEnd(Ng);
}

template <class V>
MAPTYPE(V::RMap, Branch) Branch::traverse(V &Vs) {
  auto Nc  = Vs.handleTraverse(&Condition);
  auto Ntb = Vs.handleTraverseWeak(&Branches[0]);
  auto Nte = Vs.handleTraverseWeak(&Branches[1]);
  return Vs.reduceBranch(*this, Nc, Ntb, Nte);
}

template <class V>
MAPTYPE(V::RMap, Return) Return::traverse(V &Vs) {
  auto Ne = Vs.handleTraverse(&Retval);
  return Vs.reduceReturn(*this, Ne);
}

template <class V>
MAPTYPE(V::RMap, BasicBlock) BasicBlock::traverse(V &Vs) {
  auto Nb = Vs.reduceBasicBlockBegin(*this);
  for (Phi *A : Args) {
    // Use TRV_SubExpr to force traversal of arguments
    Vs.handleBBArg(*A, Vs.traverse(A, TRV_SubExpr));
  }
  for (Instruction *I : Instrs) {
    // Use TRV_SubExpr to force traversal of instructions
    Vs.handleBBInstr(*I, Vs.traverse(I, TRV_SubExpr));
  }
  auto Nt = Vs.traverse(TermInstr, TRV_SubExpr);
  return Vs.reduceBasicBlockEnd(Nb, Nt);
}

template <class V>
MAPTYPE(V::RMap, SCFG) SCFG::traverse(V &Vs) {
  auto Ns = Vs.reduceSCFG_Begin(*this);
  for (BasicBlock *B : Blocks) {
    Vs.handleCFGBlock(*B, Vs.traverse(B, TRV_SubExpr));
  }
  return Vs.reduceSCFG_End(Ns);
}

template <class V>
MAPTYPE(V::RMap, Future) Future::traverse(V &Vs) {
  assert(Result && "Cannot traverse Future that has not been forced.");
  return Vs.handleTraverse(&Result);
}

template <class V>
MAPTYPE(V::RMap, Undefined) Undefined::traverse(V &Vs) {
  return Vs.reduceUndefined(*this);
}

template <class V>
MAPTYPE(V::RMap, Wildcard) Wildcard::traverse(V &Vs) {
  return Vs.reduceWildcard(*this);
}

template <class V>
MAPTYPE(V::RMap, Identifier)
Identifier::traverse(V &Vs) {
  return Vs.reduceIdentifier(*this);
}

template <class V>
MAPTYPE(V::RMap, Let) Let::traverse(V &Vs) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.handleTraverse(&VDecl, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the let variable.
  Vs.enterScope(VDecl, E0);
  auto E1 = Vs.handleTraverse(&Body, TRV_Tail);
  Vs.exitScope(VDecl);
  return Vs.reduceLet(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, Letrec) Letrec::traverse(V &Vs) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.handleTraverse(&VDecl, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the let variable.
  Vs.enterScope(VDecl, E0);
  auto E1 = Vs.handleTraverse(&Body, TRV_Tail);
  Vs.exitScope(VDecl);
  return Vs.reduceLetrec(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, IfThenElse) IfThenElse::traverse(V &Vs) {
  auto Nc = Vs.handleTraverse(&Condition);
  auto Nt = Vs.handleTraverse(&ThenExpr, TRV_Tail);
  auto Ne = Vs.handleTraverse(&ElseExpr, TRV_Tail);
  return Vs.reduceIfThenElse(*this, Nc, Nt, Ne);
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
  Cmp.exitScope();
  return Ct;
}

template <class C>
typename C::CType SFunction::compare(const SFunction* E, C& Cmp) const {
  Cmp.enterScope(variableDecl(), E->variableDecl());
  typename C::CType Ct = Cmp.compare(body(), E->body());
  Cmp.exitScope();
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
typename C::CType Slot::compare(const Slot* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compareStrings(name(), E->name());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(definition(), E->definition());
}

template <class C>
typename C::CType Record::compare(const Record* E, C& Cmp) const {
  unsigned N = slots().size();
  unsigned M = E->slots().size();
  typename C::CType Ct = Cmp.compareIntegers(N, M);
  if (Cmp.notTrue(Ct))
    return Ct;
  unsigned Sz = (N < M) ? N : M;
  for (unsigned i = 0; i < Sz; ++i) {
    Ct = Cmp.compare(slots()[i], E->slots()[i]);
    if (Cmp.notTrue(Ct))
      return Ct;
  }
  return Ct;
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
  return Cmp.compare(initializer(), E->initializer());
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
  Cmp.exitScope();
  return Ct;
}

template <class C>
typename C::CType Letrec::compare(const Letrec* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(variableDecl(), E->variableDecl());
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.exitScope();
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
  void exitScope() { }

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
  void exitScope() { }

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
