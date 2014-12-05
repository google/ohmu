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
  TRV_Instr, ///< owned subexpr in basic block
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
  MAPTYPE(RMap, SExpr) traverseAll(SExpr* E) {
    return self()->traverse(E, TRV_Tail);
  }

  /// Invoked by SExpr classes to traverse possibly weak members.
  /// Do not override.
  MAPTYPE(RMap, SExpr) traverseArg(SExpr* E) {
    // Detect weak references to other instructions in the CFG.
    if (Instruction *I = E->asCFGInstruction())
      return self()->reduceWeak(I);
    return self()->traverse(E, TRV_Arg);
  }

  /// Invoked by SExpr classes to traverse weak data members.
  /// Do not override.
  template <class T>
  MAPTYPE(RMap, T) traverseWeak(T* E) {
    return self()->reduceWeak(E);
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
#include "TILOps.def"
#undef TIL_OPCODE_DEF


protected:
  /// For generic SExprs, do dynamic dispatch by type.
  MAPTYPE(RMap, SExpr) traverseByType(SExpr* E, TraversalKind K) {
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
  MAPTYPE(RMap, X) traverseByType(X* E, TraversalKind K) {                \
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

template <class V>
MAPTYPE(V::RMap, VarDecl) VarDecl::traverse(V &Vs) {
  switch (kind()) {
    case VK_Fun: {
      auto D = Vs.traverse(Definition.get(), TRV_Type);
      return Vs.reduceVarDecl(*this, D);
    }
    case VK_SFun: {
      // Don't traverse the definition, since it cyclicly points back to self.
      // Just create a new (dummy) definition.
      return Vs.reduceVarDecl(*this, V::RMap::reduceNull());
    }
    case VK_Let: {
      auto D = Vs.traverse(Definition.get(), TRV_Decl);
      return Vs.reduceVarDecl(*this, D);
    }
    case VK_Letrec: {
      // Create a new (empty) definition.
      auto Nvd = Vs.reduceVarDecl(*this, V::RMap::reduceNull());
      // Enter the scope of the empty definition.
      Vs.enterScope(this, Nvd);
      // Traverse the definition, and hope recursive references are lazy.
      auto D = Vs.traverse(Definition.get(), TRV_Decl);
      Vs.exitScope(this);
      return Vs.reduceVarDeclLetrec(Nvd, D);
    }
  }
}

template <class V>
MAPTYPE(V::RMap, Function) Function::traverse(V &Vs) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverse(VDecl.get(), TRV_Decl);
  // Tell the rewriter to enter the scope of the function.
  Vs.enterScope(VDecl.get(), E0);
  auto E1 = Vs.traverse(Body.get(), TRV_Decl);
  Vs.exitScope(VDecl.get());
  return Vs.reduceFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, Code) Code::traverse(V &Vs) {
  auto Nt = Vs.traverse(ReturnType.get(), TRV_Type);
  auto Nb = Vs.traverse(Body.get(),       TRV_Lazy);
  return Vs.reduceCode(*this, Nt, Nb);
}

template <class V>
MAPTYPE(V::RMap, Field) Field::traverse(V &Vs) {
  auto Nr = Vs.traverse(Range.get(), TRV_Type);
  auto Nb = Vs.traverse(Body.get(),  TRV_Lazy);
  return Vs.reduceField(*this, Nr, Nb);
}

template <class V>
MAPTYPE(V::RMap, Slot) Slot::traverse(V &Vs) {
  auto Nd = Vs.traverse(Definition.get(), TRV_Decl);
  return Vs.reduceSlot(*this, Nd);
}

template <class V>
MAPTYPE(V::RMap, Record) Record::traverse(V &Vs) {
  auto Nr = Vs.reduceRecordBegin(*this);
  for (auto &Slt : Slots) {
    Vs.handleRecordSlot(Nr, Vs.traverse(Slt.get(), TRV_Decl));
  }
  return Vs.reduceRecordEnd(Nr);
}

template<class V>
MAPTYPE(V::RMap, ScalarType) ScalarType::traverse(V &Vs) {
  return Vs.reduceScalarType(*this);
}


template <class V>
MAPTYPE(V::RMap, Literal) Literal::traverse(V &Vs) {
  switch (BType.Base) {
  case BaseType::BT_Void:
    break;
  case BaseType::BT_Bool:
    return Vs.reduceLiteralT(as<bool>());
  case BaseType::BT_Int: {
    switch (BType.Size) {
    case BaseType::ST_8:
      return Vs.reduceLiteralT(as<int8_t>());
    case BaseType::ST_16:
      return Vs.reduceLiteralT(as<int16_t>());
    case BaseType::ST_32:
      return Vs.reduceLiteralT(as<int32_t>());
    case BaseType::ST_64:
      return Vs.reduceLiteralT(as<int64_t>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_UnsignedInt: {
    switch (BType.Size) {
    case BaseType::ST_8:
      return Vs.reduceLiteralT(as<uint8_t>());
    case BaseType::ST_16:
      return Vs.reduceLiteralT(as<uint16_t>());
    case BaseType::ST_32:
      return Vs.reduceLiteralT(as<uint32_t>());
    case BaseType::ST_64:
      return Vs.reduceLiteralT(as<uint64_t>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_Float: {
    switch (BType.Size) {
    case BaseType::ST_32:
      return Vs.reduceLiteralT(as<float>());
    case BaseType::ST_64:
      return Vs.reduceLiteralT(as<double>());
    default:
      break;
    }
    break;
  }
  case BaseType::BT_String:
    return Vs.reduceLiteralT(as<StringRef>());
  case BaseType::BT_Pointer:
    return Vs.reduceLiteralT(as<void*>());
  }
  return Vs.reduceLiteral(*this);
}

template<class V>
MAPTYPE(V::RMap, Variable) Variable::traverse(V &Vs) {
  return Vs.reduceVariable(*this, Vs.traverseWeak(VDecl.get()));
}

template <class V>
MAPTYPE(V::RMap, Apply) Apply::traverse(V &Vs) {
  auto Nf = Vs.traverse(Fun.get(), TRV_Path);
  auto Na = Arg.get() ? Vs.traverseArg(Arg.get())
                      : V::RMap::reduceNull();
  return Vs.reduceApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RMap, Project) Project::traverse(V &Vs) {
  auto Nr = Vs.traverse(Rec.get(), TRV_Path);
  return Vs.reduceProject(*this, Nr);
}

template <class V>
MAPTYPE(V::RMap, Call) Call::traverse(V &Vs) {
  auto Nt = Vs.traverse(Target.get(), TRV_Path);
  return Vs.reduceCall(*this, Nt);
}

template <class V>
MAPTYPE(V::RMap, Alloc) Alloc::traverse(V &Vs) {
  auto Nd = Vs.traverseArg(InitExpr.get());
  return Vs.reduceAlloc(*this, Nd);
}

template <class V>
MAPTYPE(V::RMap, Load) Load::traverse(V &Vs) {
  auto Np = Vs.traverseArg(Ptr.get());
  return Vs.reduceLoad(*this, Np);
}

template <class V>
MAPTYPE(V::RMap, Store) Store::traverse(V &Vs) {
  auto Np = Vs.traverseArg(Dest.get());
  auto Nv = Vs.traverseArg(Source.get());
  return Vs.reduceStore(*this, Np, Nv);
}

template <class V>
MAPTYPE(V::RMap, ArrayIndex) ArrayIndex::traverse(V &Vs) {
  auto Na = Vs.traverseArg(Array.get());
  auto Ni = Vs.traverseArg(Index.get());
  return Vs.reduceArrayIndex(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RMap, ArrayAdd) ArrayAdd::traverse(V &Vs) {
  auto Na = Vs.traverseArg(Array.get());
  auto Ni = Vs.traverseArg(Index.get());
  return Vs.reduceArrayAdd(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RMap, UnaryOp) UnaryOp::traverse(V &Vs) {
  auto Ne = Vs.traverseArg(Expr0.get());
  return Vs.reduceUnaryOp(*this, Ne);
}

template <class V>
MAPTYPE(V::RMap, BinaryOp) BinaryOp::traverse(V &Vs) {
  auto Ne0 = Vs.traverseArg(Expr0.get());
  auto Ne1 = Vs.traverseArg(Expr1.get());
  return Vs.reduceBinaryOp(*this, Ne0, Ne1);
}

template <class V>
MAPTYPE(V::RMap, Cast) Cast::traverse(V &Vs) {
  auto Ne = Vs.traverseArg(Expr0.get());
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
  auto Ntb = Vs.traverseWeak(TargetBlock.get());
  auto Ng  = Vs.reduceGotoBegin(*this, Ntb);
  for (Phi *Ph : TargetBlock->arguments()) {
    if (Ph && Ph->instrID() > 0) {
      // Ignore any newly-added Phi nodes (e.g. from an in-place SSA pass.)
      Vs.handlePhiArg(*Ph, Ng,
        Vs.traverseArg( Ph->values()[phiIndex()].get() ));
    }
  }
  return Vs.reduceGotoEnd(Ng);
}

template <class V>
MAPTYPE(V::RMap, Branch) Branch::traverse(V &Vs) {
  auto Nc  = Vs.traverseArg(Condition.get());
  auto Ntb = Vs.traverseWeak(Branches[0].get());
  auto Nte = Vs.traverseWeak(Branches[1].get());
  return Vs.reduceBranch(*this, Nc, Ntb, Nte);
}

template <class V>
MAPTYPE(V::RMap, Return) Return::traverse(V &Vs) {
  auto Ne = Vs.traverseArg(Retval.get());
  return Vs.reduceReturn(*this, Ne);
}

template <class V>
MAPTYPE(V::RMap, BasicBlock) BasicBlock::traverse(V &Vs) {
  auto Nb = Vs.reduceBasicBlockBegin(*this);
  for (Phi *A : Args) {
    // Use TRV_SubExpr to force traversal of arguments
    Vs.handleBBArg(*A, Vs.traverse(A, TRV_Instr));
  }
  for (Instruction *I : Instrs) {
    // Use TRV_SubExpr to force traversal of instructions
    Vs.handleBBInstr(*I, Vs.traverse(I, TRV_Instr));
  }
  auto Nt = Vs.traverse(TermInstr, TRV_Instr);
  return Vs.reduceBasicBlockEnd(Nb, Nt);
}

template <class V>
MAPTYPE(V::RMap, SCFG) SCFG::traverse(V &Vs) {
  auto Ns = Vs.reduceSCFG_Begin(*this);
  for (auto &B : Blocks) {
    Vs.handleCFGBlock(*B, Vs.traverse(B.get(), TRV_Decl));
  }
  return Vs.reduceSCFG_End(Ns);
}

template <class V>
MAPTYPE(V::RMap, Future) Future::traverse(V &Vs) {
  assert(Result && "Cannot traverse Future that has not been forced.");
  return Vs.traverseArg(Result);
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
  auto E0 = Vs.traverse(VDecl.get(), TRV_Decl);
  // Tell the rewriter to enter the scope of the let variable.
  Vs.enterScope(VDecl.get(), E0);
  auto E1 = Vs.traverse(Body.get(), TRV_Tail);
  Vs.exitScope(VDecl.get());
  return Vs.reduceLet(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, Letrec) Letrec::traverse(V &Vs) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverse(VDecl.get(), TRV_Decl);
  // Tell the rewriter to enter the scope of the let variable.
  Vs.enterScope(VDecl.get(), E0);
  auto E1 = Vs.traverse(Body.get(), TRV_Tail);
  Vs.exitScope(VDecl.get());
  return Vs.reduceLetrec(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RMap, IfThenElse) IfThenElse::traverse(V &Vs) {
  auto Nc = Vs.traverseArg(Condition.get());
  auto Nt = Vs.traverse(ThenExpr.get(), TRV_Tail);
  auto Ne = Vs.traverse(ElseExpr.get(), TRV_Tail);
  return Vs.reduceIfThenElse(*this, Nc, Nt, Ne);
}


} // end namespace til
} // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILTRAVERSE_H
