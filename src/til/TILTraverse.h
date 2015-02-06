//===- TILTraverse.h ------------------------------------------*- C++ --*-===//
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
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILTRAVERSE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILTRAVERSE_H

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
  TRV_Type   ///< owned subexpr in type position      e.g. T in  \x:T -> u
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
template <class Self, class ReducerMapType>
class Traversal {
public:
  /// The underlying reducer interface, which implements TypeMap (for MAPTYPE).
  typedef ReducerMapType RMap;

  /// Cast this to the correct type (curiously recursive template pattern.)
  Self *self() { return static_cast<Self *>(this); }

  /// Initial starting point, to be called by external routines.
  MAPTYPE(RMap, SExpr) traverseAll(SExpr *E) {
    return self()->traverse(E, TRV_Tail);
  }

  /// Invoked by SExpr classes to traverse possibly weak members.
  /// Do not override.
  MAPTYPE(RMap, SExpr) traverseArg(SExpr *E) {
    // Detect weak references to other instructions in the CFG.
    if (Instruction *I = E->asCFGInstruction())
      return self()->reduceWeak(I);
    return self()->traverse(E, TRV_Arg);
  }

  /// Invoked by SExpr classes to traverse weak data members.
  /// Do not override.
  template <class T>
  MAPTYPE(RMap, T) traverseWeak(T *E) {
    return self()->reduceWeak(E);
  }

  /// Starting point for a traversal.
  /// Override this method to traverse SExprs of arbitrary type.
  template <class T>
  MAPTYPE(RMap, T) traverse(T *E, TraversalKind K) {
    return traverseByType(E, K);
  }

  /// Override these methods to traverse a particular type of SExpr.
#define TIL_OPCODE_DEF(X)                                                 \
  MAPTYPE(RMap, X) traverse##X(X *E, TraversalKind K);
#include "TILOps.def"
#undef TIL_OPCODE_DEF


protected:
  /// For generic SExprs, do dynamic dispatch by type.
  MAPTYPE(RMap, SExpr) traverseByType(SExpr *E, TraversalKind K) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                 \
    case COP_##X:                                                         \
      return self()->traverse##X(cast<X>(E), K);
#include "TILOps.def"
#undef TIL_OPCODE_DEF
    }
    return RMap::reduceNull();
  }

  /// For SExprs of known type, do static dispatch by type.
#define TIL_OPCODE_DEF(X)                                                 \
  MAPTYPE(RMap, X) traverseByType(X *E, TraversalKind K) {                \
    return self()->traverse##X(E, K);                                     \
  }
#include "TILOps.def"
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
  void handleRecordSlot(R_Record E, R_Slot Res) { }
  R_Record reduceRecordEnd(R_Record R) { return R; }

  R_SExpr reduceScalarType(ScalarType &Orig) {
    return self()->reduceSExpr(Orig);
  }

  R_SExpr reduceLiteral(Literal &Orig) {
    return self()->reduceSExpr(Orig);
  }
  template<class T>
  R_SExpr reduceLiteralT(LiteralT<T> &Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceVariable(Variable &Orig, R_VarDecl VD) {
    return self()->reduceSExpr(Orig);
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
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
  void handlePhiArg(Phi &Orig, R_Goto NG, R_SExpr Res) { }
  R_SExpr reduceGotoEnd(R_Goto G) { return G; }

  R_Phi reducePhi(Phi &Orig) {
    return self()->reduceSExpr(Orig);
  }

  R_BasicBlock reduceBasicBlockBegin(BasicBlock &Orig) {
    return self()->reduceSExpr(Orig);
  }
  void handleBBArg  (Phi &Orig,         R_SExpr Res) { }
  void handleBBInstr(Instruction &Orig, R_SExpr Res) { }
  R_BasicBlock reduceBasicBlockEnd(R_BasicBlock BB, R_SExpr Tm) { return BB; }

  R_SCFG reduceSCFG_Begin(SCFG &Orig) {
    return self()->reduceSExpr(Orig);
  }
  void handleCFGBlock(BasicBlock &Orig,  R_BasicBlock Res) { }
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

template <class S, class R>
MAPTYPE(R, VarDecl)
Traversal<S, R>::traverseVarDecl(VarDecl *E, TraversalKind K) {
  switch (E->kind()) {
    case VarDecl::VK_Fun: {
      auto D = self()->traverse(E->definition(), TRV_Type);
      return self()->reduceVarDecl(*E, D);
    }
    case VarDecl::VK_SFun: {
      // Don't traverse the definition, since it cyclicly points back to self.
      // Just create a new (dummy) definition.
      return self()->reduceVarDecl(*E, RMap::reduceNull());
    }
    case VarDecl::VK_Let: {
      auto D = self()->traverse(E->definition(), TRV_Decl);
      return self()->reduceVarDecl(*E, D);
    }
    case VarDecl::VK_Letrec: {
      // Create a new (empty) definition.
      auto Nvd = self()->reduceVarDecl(*E, RMap::reduceNull());
      // Enter the scope of the empty definition.
      self()->enterScope(E, Nvd);
      // Traverse the definition, and hope recursive references are lazy.
      auto D = self()->traverse(E->definition(), TRV_Decl);
      self()->exitScope(E);
      return self()->reduceVarDeclLetrec(Nvd, D);
    }
  }
}

template <class S, class R>
MAPTYPE(R, Function)
Traversal<S, R>::traverseFunction(Function *E, TraversalKind K) {
  // E is a variable declaration, so traverse the definition.
  auto E0 = self()->traverse(E->variableDecl(), TRV_Decl);
  // Tell the rewriter to enter the scope of the function.
  self()->enterScope(E->variableDecl(), E0);
  auto E1 = self()->traverse(E->body(), TRV_Decl);
  self()->exitScope(E->variableDecl());
  return self()->reduceFunction(*E, E0, E1);
}

template <class S, class R>
MAPTYPE(R, Code)
Traversal<S, R>::traverseCode(Code *E, TraversalKind K) {
  auto Nt = self()->traverse(E->returnType(), TRV_Type);
  auto Nb = self()->traverse(E->body(),       TRV_Lazy);
  return self()->reduceCode(*E, Nt, Nb);
}

template <class S, class R>
MAPTYPE(R, Field)
Traversal<S, R>::traverseField(Field *E, TraversalKind K) {
  auto Nr = self()->traverse(E->range(), TRV_Type);
  auto Nb = self()->traverse(E->body(),  TRV_Lazy);
  return self()->reduceField(*E, Nr, Nb);
}

template <class S, class R>
MAPTYPE(R, Slot)
Traversal<S, R>::traverseSlot(Slot *E, TraversalKind K) {
  auto Nd = self()->traverse(E->definition(), TRV_Decl);
  return self()->reduceSlot(*E, Nd);
}

template <class S, class R>
MAPTYPE(R, Record)
Traversal<S, R>::traverseRecord(Record *E, TraversalKind K) {
  auto Nr = self()->reduceRecordBegin(*E);
  for (auto &Slt : E->slots()) {
    self()->handleRecordSlot(Nr, self()->traverse(Slt.get(), TRV_Decl));
  }
  return self()->reduceRecordEnd(Nr);
}

template <class S, class R>
MAPTYPE(R, ScalarType)
Traversal<S, R>::traverseScalarType(ScalarType *E, TraversalKind K) {
  return self()->reduceScalarType(*E);
}


template <class S, class R>
MAPTYPE(R, Literal)
Traversal<S, R>::traverseLiteral(Literal *E, TraversalKind K) {
  switch (E->baseType().Base) {
  case BaseType::BT_Void:
    break;
  case BaseType::BT_Bool:
    return self()->reduceLiteralT(E->as<bool>());
  case BaseType::BT_Int: {
    switch (E->baseType().Size) {
    case BaseType::ST_8:
      return self()->reduceLiteralT(E->as<int8_t>());
    case BaseType::ST_16:
      return self()->reduceLiteralT(E->as<int16_t>());
    case BaseType::ST_32:
      return self()->reduceLiteralT(E->as<int32_t>());
    case BaseType::ST_64:
      return self()->reduceLiteralT(E->as<int64_t>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_UnsignedInt: {
    switch (E->baseType().Size) {
    case BaseType::ST_8:
      return self()->reduceLiteralT(E->as<uint8_t>());
    case BaseType::ST_16:
      return self()->reduceLiteralT(E->as<uint16_t>());
    case BaseType::ST_32:
      return self()->reduceLiteralT(E->as<uint32_t>());
    case BaseType::ST_64:
      return self()->reduceLiteralT(E->as<uint64_t>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_Float: {
    switch (E->baseType().Size) {
    case BaseType::ST_32:
      return self()->reduceLiteralT(E->as<float>());
    case BaseType::ST_64:
      return self()->reduceLiteralT(E->as<double>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_String:
    return self()->reduceLiteralT(E->as<StringRef>());
  case BaseType::BT_Pointer:
    return self()->reduceLiteralT(E->as<void*>());
  }
  return self()->reduceLiteral(*E);
}

template <class S, class R>
MAPTYPE(R, Variable)
Traversal<S, R>::traverseVariable(Variable *E, TraversalKind K) {
  auto D = self()->traverseWeak(E->variableDecl());
  return self()->reduceVariable(*E, D);
}

template <class S, class R>
MAPTYPE(R, Apply) Traversal<S, R>::traverseApply(Apply *E, TraversalKind K) {
  auto Nf = self()->traverse(E->fun(), TRV_Path);
  auto Na = E->arg() ? self()->traverseArg(E->arg())
                     : RMap::reduceNull();
  return self()->reduceApply(*E, Nf, Na);
}

template <class S, class R>
MAPTYPE(R, Project)
Traversal<S, R>::traverseProject(Project *E, TraversalKind K) {
  auto Nr = self()->traverse(E->record(), TRV_Path);
  return self()->reduceProject(*E, Nr);
}

template <class S, class R>
MAPTYPE(R, Call) Traversal<S, R>::traverseCall(Call *E, TraversalKind K) {
  auto Nt = self()->traverse(E->target(), TRV_Path);
  return self()->reduceCall(*E, Nt);
}

template <class S, class R>
MAPTYPE(R, Alloc) Traversal<S, R>::traverseAlloc(Alloc *E, TraversalKind K) {
  auto Nd = self()->traverseArg(E->initializer());
  return self()->reduceAlloc(*E, Nd);
}

template <class S, class R>
MAPTYPE(R, Load) Traversal<S, R>::traverseLoad(Load *E, TraversalKind K) {
  auto Np = self()->traverseArg(E->pointer());
  return self()->reduceLoad(*E, Np);
}

template <class S, class R>
MAPTYPE(R, Store) Traversal<S, R>::traverseStore(Store *E, TraversalKind K) {
  auto Np = self()->traverseArg(E->destination());
  auto Nv = self()->traverseArg(E->source());
  return self()->reduceStore(*E, Np, Nv);
}

template <class S, class R>
MAPTYPE(R, ArrayIndex)
Traversal<S, R>::traverseArrayIndex(ArrayIndex *E, TraversalKind K) {
  auto Na = self()->traverseArg(E->array());
  auto Ni = self()->traverseArg(E->index());
  return self()->reduceArrayIndex(*E, Na, Ni);
}

template <class S, class R>
MAPTYPE(R, ArrayAdd)
Traversal<S, R>::traverseArrayAdd(ArrayAdd *E, TraversalKind K) {
  auto Na = self()->traverseArg(E->array());
  auto Ni = self()->traverseArg(E->index());
  return self()->reduceArrayAdd(*E, Na, Ni);
}

template <class S, class R>
MAPTYPE(R, UnaryOp)
Traversal<S, R>::traverseUnaryOp(UnaryOp *E, TraversalKind K) {
  auto Ne = self()->traverseArg(E->expr());
  return self()->reduceUnaryOp(*E, Ne);
}

template <class S, class R>
MAPTYPE(R, BinaryOp)
Traversal<S, R>::traverseBinaryOp(BinaryOp *E, TraversalKind K) {
  auto Ne0 = self()->traverseArg(E->expr0());
  auto Ne1 = self()->traverseArg(E->expr1());
  return self()->reduceBinaryOp(*E, Ne0, Ne1);
}

template <class S, class R>
MAPTYPE(R, Cast)
Traversal<S, R>::traverseCast(Cast *E, TraversalKind K) {
  auto Ne = self()->traverseArg(E->expr());
  return self()->reduceCast(*E, Ne);
}

template <class S, class R>
MAPTYPE(R, Phi) Traversal<S, R>::traversePhi(Phi *E, TraversalKind K) {
  // Note: traversing a Phi does not traverse it's arguments.
  // The arguments are traversed by the Goto.
  return self()->reducePhi(*E);
}

template <class S, class R>
MAPTYPE(R, Goto) Traversal<S, R>::traverseGoto(Goto *E, TraversalKind K) {
  auto Ntb = self()->traverseWeak(E->targetBlock());
  auto Ng  = self()->reduceGotoBegin(*E, Ntb);
  for (Phi *Ph : E->targetBlock()->arguments()) {
    if (Ph && Ph->instrID() > 0) {
      // Ignore any newly-added Phi nodes (e.g. from an in-place SSA pass.)
      auto A = self()->traverseArg( Ph->values()[E->phiIndex()].get() );
      self()->handlePhiArg(*Ph, Ng, A);
    }
  }
  return self()->reduceGotoEnd(Ng);
}

template <class S, class R>
MAPTYPE(R, Branch) Traversal<S, R>::traverseBranch(Branch *E, TraversalKind K) {
  auto Nc  = self()->traverseArg(E->condition());
  auto Ntb = self()->traverseWeak(E->thenBlock());
  auto Nte = self()->traverseWeak(E->elseBlock());
  return self()->reduceBranch(*E, Nc, Ntb, Nte);
}

template <class S, class R>
MAPTYPE(R, Return) Traversal<S, R>::traverseReturn(Return *E, TraversalKind K) {
  auto Ne = self()->traverseArg(E->returnValue());
  return self()->reduceReturn(*E, Ne);
}

template <class S, class R>
MAPTYPE(R, BasicBlock)
Traversal<S, R>::traverseBasicBlock(BasicBlock *E, TraversalKind K) {
  auto Nb = self()->reduceBasicBlockBegin(*E);
  for (Phi *A : E->arguments()) {
    // Use TRV_SubExpr to force traversal of arguments
    self()->handleBBArg(*A, self()->traverse(A, TRV_Instr));
  }
  for (Instruction *I : E->instructions()) {
    // Use TRV_SubExpr to force traversal of instructions
    self()->handleBBInstr(*I, self()->traverse(I, TRV_Instr));
  }
  auto Nt = self()->traverse(E->terminator(), TRV_Instr);
  return self()->reduceBasicBlockEnd(Nb, Nt);
}

template <class S, class R>
MAPTYPE(R, SCFG) Traversal<S, R>::traverseSCFG(SCFG *E, TraversalKind K) {
  auto Ns = self()->reduceSCFG_Begin(*E);
  for (auto &B : E->blocks()) {
    self()->handleCFGBlock(*B, self()->traverse(B.get(), TRV_Decl));
  }
  return self()->reduceSCFG_End(Ns);
}

template <class S, class R>
MAPTYPE(R, Future) Traversal<S, R>::traverseFuture(Future *E, TraversalKind K) {
  auto *Res = E->maybeGetResult();
  assert(Res && "Cannot traverse Future that has not been forced.");
  return self()->traverseArg(Res);
}

template <class S, class R>
MAPTYPE(R, Undefined)
Traversal<S, R>::traverseUndefined(Undefined *E, TraversalKind K) {
  return self()->reduceUndefined(*E);
}

template <class S, class R>
MAPTYPE(R, Wildcard)
Traversal<S, R>::traverseWildcard(Wildcard *E, TraversalKind K) {
  return self()->reduceWildcard(*E);
}

template <class S, class R>
MAPTYPE(R, Identifier)
Traversal<S, R>::traverseIdentifier(Identifier *E, TraversalKind K) {
  return self()->reduceIdentifier(*E);
}

template <class S, class R>
MAPTYPE(R, Let) Traversal<S, R>::traverseLet(Let *E, TraversalKind K) {
  // E is a variable declaration, so traverse the definition.
  auto E0 = self()->traverse(E->variableDecl(), TRV_Decl);
  // Tell the rewriter to enter the scope of the let variable.
  self()->enterScope(E->variableDecl(), E0);
  auto E1 = self()->traverse(E->body(), TRV_Tail);
  self()->exitScope(E->variableDecl());
  return self()->reduceLet(*E, E0, E1);
}

template <class S, class R>
MAPTYPE(R, Letrec) Traversal<S, R>::traverseLetrec(Letrec *E, TraversalKind K) {
  // E is a variable declaration, so traverse the definition.
  auto E0 = self()->traverse(E->variableDecl(), TRV_Decl);
  // Tell the rewriter to enter the scope of the let variable.
  self()->enterScope(E->variableDecl(), E0);
  auto E1 = self()->traverse(E->body(), TRV_Tail);
  self()->exitScope(E->variableDecl());
  return self()->reduceLetrec(*E, E0, E1);
}

template <class S, class R>
MAPTYPE(R, IfThenElse)
Traversal<S, R>::traverseIfThenElse(IfThenElse *E, TraversalKind K) {
  auto Nc = self()->traverseArg(E->condition());
  auto Nt = self()->traverse(E->thenExpr(), TRV_Tail);
  auto Ne = self()->traverse(E->elseExpr(), TRV_Tail);
  return self()->reduceIfThenElse(*E, Nc, Nt, Ne);
}


} // end namespace til
} // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILTRAVERSE_H
