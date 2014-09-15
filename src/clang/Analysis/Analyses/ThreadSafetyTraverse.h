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

enum TraversalKind {
  TRV_Normal,   ///< argument subexpression (the default)
  TRV_SubExpr,  ///< owned subexpression
  TRV_Lazy,     ///< subexpression that requires lazy rewriting (e.g. code)
  TRV_Type      ///< type expressions
};


/// Traversal defines an interface for traversing SExprs.  Traversals have
/// been made as generic as possible, and are intended to handle any kind of
/// pass over the AST, e.g. visiters, copying, non-destructive rewriting,
/// destructive (in-place) rewriting, hashing, typing, etc.
///
/// The Traversal class is responsible for traversing the AST in some order.
/// The default is a depth first traversal, but other orders are possible,
/// such as lazy or parallel traversals.  The traversal will process the AST
/// by invoking methods on a context.
///
/// A Context is a pointer (or smart pointer) to a Reducer object, which is
/// responsible for two separate tasks:
/// (1) tracking lexical scope via enterX/exitX methods, and
/// (2) implementing reduceX methods to rewrite the AST.
///
/// Lexical scope consists of information such as the types of variables,
/// the current CFG, etc.  The traversal infrastructure will call enter/exit
/// methods as it traverses the program to update the scope.  These methods
/// may clone the context or create a new one.
///
/// The reduceX methods are responsible for rewriting terms.  After an SExpr
/// has been traversed, the traversal results are passed to reduceX(...), which
/// essentially implements an SExpr builder API.  A normal transform pass will
/// implement this API to build a rewritten SExpr, but other passes may build
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
  /// The underlying reducer interface, which implements TypeMap.
  typedef ReducerT RedT;

  /// The context type, which is passed by value.
  typedef typename ReducerT::ContextT CtxT;

  /// Traversal will reduce SExprs to objects of type R_SExpr.
  typedef MAPTYPE(RedT, SExpr) R_SExpr;

  /// Cast this to the correct type (curiously recursive template pattern.)
  Self *self() { return static_cast<Self *>(this); }

  /// Entry point for traversing an SExpr of unknown type.
  /// This will dispatch on the type of E, and call traverseX.
  R_SExpr traverse(SExpr *E, CtxT Ctx, TraversalKind K = TRV_Normal) {
    // Avoid infinite loops.
    // E->block() is true if E is an instruction in a basic block.
    // When traversing the instr from the basic block, then K == TRV_SubExpr.
    // Indirect references to the instruction will have K == TRV_Normal.
    if (K == TRV_Normal && E->block())
      return self()->traverseWeakInstr(E, Ctx);
    return self()->traverseByCase(E, Ctx, K);
  }

  /// Helper method to call traverseX(e) on the appropriate type.
  R_SExpr traverseByCase(SExpr *E, CtxT Ctx, TraversalKind K = TRV_Normal) {
    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                   \
    case COP_##X:                                                           \
      return self()->traverse##X(cast<X>(E), Ctx, K);
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }
    return Ctx->reduceNull();
  }

  // Traverse e, by static dispatch on the type "X" of e.
  // The return type for the reduce depends on X.
  // Override these methods to control traversal of a particular kind of term.
#define TIL_OPCODE_DEF(X)                                                     \
  MAPTYPE(RedT,X) traverse##X(X *e, CtxT Ctx, TraversalKind K = TRV_Normal) { \
    return e->traverse(*self(), Ctx);                                         \
  }
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF

  /// Entry point for traversing a weak reference to an SExpr.
  /// Override this to handle weak (non-owning) references to instructions.
  MAPTYPE(RedT, SExpr) traverseWeakInstr(SExpr *E, CtxT Ctx) {
    return Ctx->reduceWeakInstr(E);
  }

  /// Entry point for traversing a reference to a declared variable.
  MAPTYPE(RedT, VarDecl) traverseWeakVarDecl(VarDecl *D, CtxT Ctx) {
    return Ctx->reduceWeakVarDecl(D);
  }

  /// Entry point for traversing a branch target.
  MAPTYPE(RedT, BasicBlock) traverseWeakBasicBlock(BasicBlock *B, CtxT Ctx) {
    return Ctx->reduceWeakBasicBlock(B);
  }
};


/// Implements enter/exit methods for a reducer that doesn't care about scope.
/// The context is a simple pointer to the reducer object.
/// This is a mixin class which inherits from R, which should be a ReducerBase.
template <class Self, class RedT>
class UnscopedReducer : public RedT {
public:
  typedef Self* ContextT;

  Self* self() { return static_cast<Self*>(this); }

  Self* enterScope(VarDecl* Orig, MAPTYPE(RedT, VarDecl) Nvd) {
    return self();
  }
  Self* exitScope(VarDecl* Orig) {
    return self();
  }

  Self* enterBasicBlock(BasicBlock* Orig, MAPTYPE(RedT, BasicBlock) Nbb) {
    return self();
  }
  Self* exitBasicBlock(BasicBlock* Orig) {
    return self();
  }

  Self* enterCFG(SCFG* Orig, MAPTYPE(RedT, SCFG) Nscfg) {
    return self();
  }
  Self* exitCFG(SCFG* Orig) {
    return self();
  }
};


/// Implements reduceX methods for a simple visitor.   A visitor "rewrites"
/// SExprs to booleans: it returns true on success, and false on failure.
class VisitReducerBase {
public:
  VisitReducerBase() {}

  // A visitor maps all expression types to bool.
  template <class T> struct TypeMap { typedef bool Ty; };

public:
  bool reduceNull() { return true; }

  bool reduceWeakInstr(SExpr* E)      { return true; }
  bool reduceWeakVarDecl(SExpr *E)    { return true; }
  bool reduceWeakBasicBlock(SExpr *E) { return true; }

  bool reduceLiteral(Literal &Orig)       { return true; }
  template<class T>
  bool reduceLiteralT(LiteralT<T> &Orig)  { return true; }
  bool reduceLiteralPtr(LiteralPtr &Orig) { return true; }

  bool reduceVarDecl(VarDecl &Orig, bool E)                 { return true; }
  bool reduceFunction(Function &Orig, bool E0, bool E1)     { return true; }
  bool reduceSFunction(SFunction &Orig, bool E0, bool E1)   { return true; }
  bool reduceCode(Code &Orig, bool E0, bool E1)             { return true; }
  bool reduceField(Field &Orig, bool E0, bool E1)           { return true; }
  bool reduceApply(Apply &Orig, bool E0, bool E1)           { return true; }
  bool reduceSApply(SApply &Orig, bool E0, bool E1)         { return true; }

  bool reduceProject(Project &Orig, bool E0)                { return true; }
  bool reduceCall(Call &Orig, bool E0)                      { return true; }
  bool reduceAlloc(Alloc &Orig, bool E0)                    { return true; }
  bool reduceLoad(Load &Orig, bool E0)                      { return true; }
  bool reduceStore(Store &Orig, bool E0, bool E1)           { return true; }
  bool reduceArrayIndex(ArrayIndex &Orig, bool E0, bool E1) { return true; }
  bool reduceArrayAdd(ArrayAdd &Orig, bool E0, bool E1)     { return true; }

  bool reduceUnaryOp(UnaryOp &Orig, bool E0)                { return true; }
  bool reduceBinaryOp(BinaryOp &Orig, bool E0, bool E1)     { return true; }
  bool reduceCast(Cast &Orig, bool E0)                      { return true; }

  bool reducePhiBegin(Phi &Orig) { return true; }
  void reducePhiArg(bool Ph, unsigned i, bool E) { }
  bool reducePhi(bool Ph) { return Ph; }

  bool reduceSCFGBegin(SCFG &Orig) { return true; }
  void reduceSCFGBlock(bool Scfg, unsigned i, bool E) { }
  bool reduceSCFG(bool S) { return S; }

  bool reduceBasicBlockBegin(BasicBlock &Orig) { return true; }
  void reduceBasicBlockArg  (bool BB, unsigned i, bool E)  { }
  void reduceBasicBlockInstr(bool BB, unsigned i, bool E)  { }
  void reduceBasicBlockTerm (bool BB, bool E)              { }
  bool reduceBasicBlock     (bool BB) { return BB; }

  bool reduceGoto(Goto &Orig, bool B)                    { return true; }
  bool reduceBranch(Branch &O, bool C, bool B0, bool B1) { return true; }
  bool reduceReturn(Return &O, bool E)                   { return true; }

  bool reduceUndefined(Undefined &Orig)   { return true; }
  bool reduceWildcard(Wildcard &Orig)     { return true; }
  bool reduceIdentifier(Identifier &Orig) { return true; }
  bool reduceIfThenElse(IfThenElse &Orig, bool C, bool T, bool E) {
    return true;
  }
  bool reduceLet(Let &Orig, bool E0, bool E1) { return true; }
};


/// Specialize Traversal to abort on failure.
template<class Self, class ReducerT>
class VisitTraversal : public Traversal<Self, ReducerT> {
public:
  typedef typename ReducerT::ContextT CtxT;

  VisitTraversal() : Success(true) { }

  Self *self() { return static_cast<Self *>(this); }

  /// Override traverse to set success, and bail on failure.
  bool traverse(SExpr *E, CtxT Ctx, TraversalKind K = TRV_Normal) {
    if (K == TRV_Normal && E->block())
      return self()->traverseWeakInstr(E, Ctx);
    return Success = Success && self()->traverseByCase(E, Ctx, K);
  }

private:
  bool Success;
};


// Used by CopyReducer.  Most terms map to SExpr*.
template <class T> struct DefaultTypeMap { typedef SExpr* Ty; };

// These kinds of SExpr must map to the same kind.
// We define these here b/c template specializations cannot be class members.
template<> struct DefaultTypeMap<VarDecl>    { typedef VarDecl* Ty; };
template<> struct DefaultTypeMap<BasicBlock> { typedef BasicBlock* Ty; };
template<> struct DefaultTypeMap<SCFG>       { typedef SCFG* Ty; };


class CopyReducerBase {
public:
  // R_SExpr is the result type for a traversal.
  // A copy reducer returns a newly allocated term.
  template <class T> struct TypeMap : public DefaultTypeMap<T> { };

  CopyReducerBase() {}
  CopyReducerBase(MemRegionRef A) : Arena(A) { }

  void setArena(MemRegionRef A) { Arena = A; }

public:
  SExpr* reduceNull() { return nullptr; }

  SExpr* reduceWeakInstr(SExpr* E)                { return nullptr; }
  VarDecl* reduceWeakVarDecl(VarDecl *E)          { return nullptr; }
  BasicBlock* reduceWeakBasicBlock(BasicBlock *E) { return nullptr; }

  SExpr* reduceLiteral(Literal &Orig) {
    return new (Arena) Literal(Orig);
  }
  template<class T>
  SExpr* reduceLiteralT(LiteralT<T> &Orig) {
    return new (Arena) LiteralT<T>(Orig);
  }
  SExpr* reduceLiteralPtr(LiteralPtr &Orig) {
    return new (Arena) LiteralPtr(Orig);
  }

  VarDecl* reduceVarDecl(VarDecl &Orig, SExpr* E) {
    return new (Arena) VarDecl(Orig, E);
  }
  SExpr* reduceFunction(Function &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) Function(Orig, Nvd, E0);
  }
  SExpr* reduceSFunction(SFunction &Orig, VarDecl *Nvd, SExpr* E0) {
    return new (Arena) SFunction(Orig, Nvd, E0);
  }
  SExpr* reduceCode(Code &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Code(Orig, E0, E1);
  }
  SExpr* reduceField(Field &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Field(Orig, E0, E1);
  }

  SExpr* reduceApply(Apply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Apply(Orig, E0, E1);
  }
  SExpr* reduceSApply(SApply &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) SApply(Orig, E0, E1);
  }
  SExpr* reduceProject(Project &Orig, SExpr* E0) {
    return new (Arena) Project(Orig, E0);
  }

  SExpr* reduceCall(Call &Orig, SExpr* E0) {
    return new (Arena) Call(Orig, E0);
  }
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    return new (Arena) Alloc(Orig, E0);
  }
  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    return new (Arena) Load(Orig, E0);
  }
  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) Store(Orig, E0, E1);
  }
  SExpr* reduceArrayIndex(ArrayIndex &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) ArrayIndex(Orig, E0, E1);
  }
  SExpr* reduceArrayAdd(ArrayAdd &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) ArrayAdd(Orig, E0, E1);
  }
  SExpr* reduceUnaryOp(UnaryOp &Orig, SExpr* E0) {
    return new (Arena) UnaryOp(Orig, E0);
  }
  SExpr* reduceBinaryOp(BinaryOp &Orig, SExpr* E0, SExpr* E1) {
    return new (Arena) BinaryOp(Orig, E0, E1);
  }
  SExpr* reduceCast(Cast &Orig, SExpr* E0) {
    return new (Arena) Cast(Orig, E0);
  }

  Phi* reducePhiBegin(Phi &Orig) {
    return new (Arena) Phi(Orig, Arena);
  }
  void reducePhiArg(Phi* Ph, unsigned i, SExpr* E) {
    Ph->values().push_back(E);
  }
  Phi* reducePhi(Phi* Ph) { return Ph; }

  SCFG* reduceSCFGBegin(SCFG &Orig) {
    return new (Arena) SCFG(Orig, Arena);
  }
  void reduceSCFGBlock(SCFG* Scfg, unsigned i, BasicBlock* B) {
    Scfg->add(B);
  }
  SCFG* reduceSCFG(SCFG* Scfg) { return Scfg; }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    return new (Arena) BasicBlock(Orig, Arena);
  }
  void reduceBasicBlockArg  (BasicBlock *BB, unsigned i, SExpr* E) {
    BB->addArgument(dyn_cast<Phi>(E));
  }
  void reduceBasicBlockInstr(BasicBlock *BB, unsigned i, SExpr* E) {
    BB->addInstruction(E);
  }
  void reduceBasicBlockTerm (BasicBlock *BB, SExpr* E) {
    BB->setTerminator(dyn_cast<Terminator>(E));
  }
  BasicBlock* reduceBasicBlock(BasicBlock *BB) { return BB; }

  SExpr* reduceGoto(Goto &Orig, BasicBlock *B) {
    return new (Arena) Goto(Orig, B, 0);
  }
  SExpr* reduceBranch(Branch &O, SExpr* C, BasicBlock *B0, BasicBlock *B1) {
    return new (Arena) Branch(O, C, B0, B1);
  }
  SExpr* reduceReturn(Return &O, SExpr* E) {
    return new (Arena) Return(O, E);
  }

  SExpr* reduceUndefined(Undefined &Orig) {
    return new (Arena) Undefined(Orig);
  }
  SExpr* reduceWildcard(Wildcard &Orig) {
    return new (Arena) Wildcard(Orig);
  }

  SExpr* reduceIdentifier(Identifier &Orig) {
    return new (Arena) Identifier(Orig);
  }
  SExpr* reduceLet(Let &Orig, VarDecl *Nvd, SExpr* B) {
    return new (Arena) Let(Orig, Nvd, B);
  }
  SExpr* reduceIfThenElse(IfThenElse &Orig, SExpr* C, SExpr* T, SExpr* E) {
    return new (Arena) IfThenElse(Orig, C, T, E);
  }

protected:
  MemRegionRef Arena;
};


////////////////////////////////////////
// traverse methods for all TIL classes.
////////////////////////////////////////


template <class V>
MAPTYPE(V::RedT, Literal) Literal::traverse(V &Vs, typename V::CtxT Ctx) {
  if (Cexpr)
    return Ctx->reduceLiteral(*this);

  switch (ValType.Base) {
  case ValueType::BT_Void:
    break;
  case ValueType::BT_Bool:
    return Ctx->reduceLiteralT(as<bool>());
  case ValueType::BT_Int: {
    switch (ValType.Size) {
    case ValueType::ST_8:
      if (ValType.Signed)
        return Ctx->reduceLiteralT(as<int8_t>());
      else
        return Ctx->reduceLiteralT(as<uint8_t>());
    case ValueType::ST_16:
      if (ValType.Signed)
        return Ctx->reduceLiteralT(as<int16_t>());
      else
        return Ctx->reduceLiteralT(as<uint16_t>());
    case ValueType::ST_32:
      if (ValType.Signed)
        return Ctx->reduceLiteralT(as<int32_t>());
      else
        return Ctx->reduceLiteralT(as<uint32_t>());
    case ValueType::ST_64:
      if (ValType.Signed)
        return Ctx->reduceLiteralT(as<int64_t>());
      else
        return Ctx->reduceLiteralT(as<uint64_t>());
    default:
      break;
    }
  }
  case ValueType::BT_Float: {
    switch (ValType.Size) {
    case ValueType::ST_32:
      return Ctx->reduceLiteralT(as<float>());
    case ValueType::ST_64:
      return Ctx->reduceLiteralT(as<double>());
    default:
      break;
    }
  }
  case ValueType::BT_String:
    return Ctx->reduceLiteralT(as<StringRef>());
  case ValueType::BT_Pointer:
    return Ctx->reduceLiteralT(as<void*>());
  case ValueType::BT_ValueRef:
    break;
  }
  return Ctx->reduceLiteral(*this);
}

template <class V>
MAPTYPE(V::RedT, LiteralPtr) LiteralPtr::traverse(V &Vs, typename V::CtxT Ctx) {
  return Ctx->reduceLiteralPtr(*this);
}

template <class V>
MAPTYPE(V::RedT, VarDecl) VarDecl::traverse(V &Vs, typename V::CtxT Ctx) {
  // Don't traverse the definition of a self-function, since it points
  // back to self.
  if (kind() == VK_SFun) {
    return Ctx->reduceVarDecl(*this, Ctx->reduceNull());
  }
  auto D = Vs.traverse(Definition, Ctx);
  return Ctx->reduceVarDecl(*this, D);
}

template <class V>
MAPTYPE(V::RedT, Function) Function::traverse(V &Vs, typename V::CtxT Ctx) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverseVarDecl(VDecl, Ctx, TRV_Type);
  // Tell the rewriter to enter the scope of the function.
  Ctx = Ctx->enterScope(VDecl, E0);
  auto E1 = Vs.traverse(Body, Ctx, TRV_SubExpr);
  Ctx = Ctx->exitScope(VDecl);
  return Ctx->reduceFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, SFunction) SFunction::traverse(V &Vs, typename V::CtxT Ctx) {
  // Traversing an self-definition is a no-op.
  auto E0 = Vs.traverseVarDecl(VDecl, Ctx, TRV_Type);
  Ctx = Ctx->enterScope(VDecl, E0);
  auto E1 = Vs.traverse(Body, Ctx, TRV_SubExpr);
  Ctx = Ctx->exitScope(VDecl);
  // The SFun constructor will set E0->Definition to E1.
  return Ctx->reduceSFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, Code) Code::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nt = Vs.traverse(ReturnType, Ctx, TRV_Type);
  auto Nb = Vs.traverse(Body,       Ctx, TRV_Lazy);
  return Ctx->reduceCode(*this, Nt, Nb);
}

template <class V>
MAPTYPE(V::RedT, Field) Field::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nr = Vs.traverse(Range, Ctx, TRV_Type);
  auto Nb = Vs.traverse(Body,  Ctx, TRV_Lazy);
  return Ctx->reduceField(*this, Nr, Nb);
}

template <class V>
MAPTYPE(V::RedT, Apply) Apply::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nf = Vs.traverse(Fun, Ctx);
  auto Na = Vs.traverse(Arg, Ctx);
  return Ctx->reduceApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, SApply) SApply::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nf = Vs.traverse(Sfun, Ctx);
  auto Na = Arg ? Vs.traverse(Arg, Ctx) : Ctx->reduceNull();
  return Ctx->reduceSApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, Project) Project::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nr = Vs.traverse(Rec, Ctx);
  return Ctx->reduceProject(*this, Nr);
}

template <class V>
MAPTYPE(V::RedT, Call) Call::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nt = Vs.traverse(Target, Ctx);
  return Ctx->reduceCall(*this, Nt);
}

template <class V>
MAPTYPE(V::RedT, Alloc) Alloc::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nd = Vs.traverse(Dtype, Ctx, TRV_SubExpr);
  return Ctx->reduceAlloc(*this, Nd);
}

template <class V>
MAPTYPE(V::RedT, Load) Load::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Vs.traverse(Ptr, Ctx);
  return Ctx->reduceLoad(*this, Np);
}

template <class V>
MAPTYPE(V::RedT, Store) Store::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Vs.traverse(Dest,   Ctx);
  auto Nv = Vs.traverse(Source, Ctx);
  return Ctx->reduceStore(*this, Np, Nv);
}

template <class V>
MAPTYPE(V::RedT, ArrayIndex) ArrayIndex::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Na = Vs.traverse(Array, Ctx);
  auto Ni = Vs.traverse(Index, Ctx);
  return Ctx->reduceArrayIndex(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, ArrayAdd) ArrayAdd::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Na = Vs.traverse(Array, Ctx);
  auto Ni = Vs.traverse(Index, Ctx);
  return Ctx->reduceArrayAdd(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, UnaryOp) UnaryOp::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(Expr0, Ctx);
  return Ctx->reduceUnaryOp(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BinaryOp) BinaryOp::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne0 = Vs.traverse(Expr0, Ctx);
  auto Ne1 = Vs.traverse(Expr1, Ctx);
  return Ctx->reduceBinaryOp(*this, Ne0, Ne1);
}

template <class V>
MAPTYPE(V::RedT, Cast) Cast::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(Expr0, Ctx);
  return Ctx->reduceCast(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, Phi) Phi::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Ctx->reducePhiBegin(*this);
  unsigned i = 0;
  for (auto *Va : Values) {
    Ctx->reducePhiArg(Np, i, Vs.traverse(Va, Ctx));
    ++i;
  }
  return Ctx->reducePhi(Np);
}

template <class V>
MAPTYPE(V::RedT, Goto) Goto::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ntb = Vs.traverseWeakBasicBlock(TargetBlock, Ctx);
  return Ctx->reduceGoto(*this, Ntb);
}

template <class V>
MAPTYPE(V::RedT, Branch) Branch::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nc  = Vs.traverse(Condition, Ctx);
  auto Ntb = Vs.traverseWeakBasicBlock(Branches[0], Ctx);
  auto Nte = Vs.traverseWeakBasicBlock(Branches[1], Ctx);
  return Ctx->reduceBranch(*this, Nc, Ntb, Nte);
}

template <class V>
MAPTYPE(V::RedT, Return) Return::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(Retval, Ctx);
  return Ctx->reduceReturn(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BasicBlock) BasicBlock::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nb = Ctx->reduceBasicBlockBegin(*this);
  Ctx = Ctx->enterBasicBlock(this, Nb);
  unsigned i = 0;
  for (auto *A : Args) {
    // Use TRV_SubExpr to force traversal of arguments
    Ctx->reduceBasicBlockArg(Nb, i, Vs.traverse(A, Ctx, TRV_SubExpr));
    ++i;
  }
  i = 0;
  for (auto *I : Instrs) {
    // Use TRV_SubExpr to force traversal of instructions
    Ctx->reduceBasicBlockInstr(Nb, i, Vs.traverse(I, Ctx, TRV_SubExpr));
    ++i;
  }
  Ctx->reduceBasicBlockTerm(Nb, Vs.traverse(TermInstr, Ctx, TRV_SubExpr));
  Ctx = Ctx->exitBasicBlock(this);

  return Ctx->reduceBasicBlock(Nb);
}

template <class V>
MAPTYPE(V::RedT, SCFG) SCFG::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ns = Ctx->reduceSCFGBegin(*this);
  Ctx = Ctx->enterCFG(this, Ns);

  unsigned i = 0;
  for (auto *B : Blocks) {
    Ctx->reduceSCFGBlock(Ns, i, Vs.traverseBasicBlock(B, Ctx, TRV_SubExpr));
    ++i;
  }
  Ctx = Ctx->exitCFG(this);
  return Ctx->reduceSCFG(Ns);
}

template <class V>
MAPTYPE(V::RedT, Future) Future::traverse(V &Vs, typename V::CtxT Ctx) {
  assert(Result && "Cannot traverse Future that has not been forced.");
  return Vs.traverse(Result, Ctx);
}

template <class V>
MAPTYPE(V::RedT, Undefined) Undefined::traverse(V &Vs, typename V::CtxT Ctx) {
  return Ctx->reduceUndefined(*this);
}

template <class V>
MAPTYPE(V::RedT, Wildcard) Wildcard::traverse(V &Vs, typename V::CtxT Ctx) {
  return Ctx->reduceWildcard(*this);
}

template <class V>
MAPTYPE(V::RedT, Identifier)
Identifier::traverse(V &Vs, typename V::CtxT Ctx) {
  return Ctx->reduceIdentifier(*this);
}

template <class V>
MAPTYPE(V::RedT, IfThenElse) IfThenElse::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nc = Vs.traverse(Condition, Ctx);
  auto Nt = Vs.traverse(ThenExpr,  Ctx);
  auto Ne = Vs.traverse(ElseExpr,  Ctx);
  return Ctx->reduceIfThenElse(*this, Nc, Nt, Ne);
}

template <class V>
MAPTYPE(V::RedT, Let) Let::traverse(V &Vs, typename V::CtxT Ctx) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverseVarDecl(VDecl, Ctx);
  // Tell the rewriter to enter the scope of the let variable.
  Ctx = Ctx->enterScope(VDecl, E0);
  auto E1 = Vs.traverse(Body, Ctx);
  Ctx = Ctx->exitScope(VDecl);
  return Ctx->reduceLet(*this, E0, E1);
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
