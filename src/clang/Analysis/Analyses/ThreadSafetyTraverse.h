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
  TRV_Normal,   ///< subexpression in argument position (the default)
  TRV_Weak,     ///< un-owned (weak) reference to subexpression
  TRV_SubExpr,  ///< owned subexpression
  TRV_Tail,     ///< owned subexpression in tail position
  TRV_Lazy,     ///< owned subexpression in lazy position
  TRV_Type      ///< owned subexpression in type position
};


void traceTraversal(const char* msg, SExpr *E);


/// The Traversal class defines an interface for traversing SExprs.  Traversals
/// have been made as generic as possible, and are intended to handle any kind
/// of pass over the AST, e.g. visiters, copiers, non-destructive rewriting,
/// destructive (in-place) rewriting, hashing, typing, garbage collection, etc.
///
/// The Traversal class is responsible for traversing the AST in some order.
/// The default is a depth first traversal, but other orders are possible,
/// such as lazy or parallel traversals.  The Traversal maintains a Context,
/// which provides method callbacks to handle various parts of the traversal.
///
/// A Context is passed by value, and is a smart pointer to a Reducer object.
/// The Reducer does most of the work, and is responsible for two tasks:
/// (1) Tracking lexical scope via enterX/exitX methods
/// (2) Implementing reduceX methods to rewrite the AST.
///
/// Lexical scope consists of information such as the types of variables,
/// the current CFG, etc.  The traversal infrastructure will invoke enter/exit
/// methods on the Context object as it traverses the program.
///
/// The reduceX methods are responsible for rewriting terms.  After an SExpr
/// has been traversed, the traversal results are passed to reduceX(...), which
/// essentially implements an SExpr builder API.  A normal transform pass will
/// use this API to build a rewritten SExpr, but other passes may build
/// an object of some other type.  In functional programming terms, reduceX
/// implements a fold operation over the AST.
///
/// Because scope and rewriting are somewhat orthogonal, the traversal
/// infrastructure provides default implementations of the enter/exit methods,
/// and reduceX methods, in separate classes.  A reducer object should be
/// constructed via multiple inheritance.
///
/// Then enter/exit methods may copy or clone the scope, and are methods of
/// Context.
///
/// The reduceX methods are part of Reducer, and are invoked via operator->
/// from Context.
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

  /// Adjust context and call traverseSExpr
  /// Note: the traverse() methods are overloaded, and cannot be overridden.
  R_SExpr traverse(SExpr** E, const CtxT &Ctx, TraversalKind K = TRV_Normal) {
    return self()->traverseSExpr(E, Ctx.sub(K), K);
  }

  /// Call traverseSExpr, and cast to appropriate type
  template<class T>
  MAPTYPE(RedT, T)
  traverse(T** E, const CtxT &Ctx, TraversalKind K = TRV_Normal) {
    CtxT NCtx = Ctx.sub(K);
    SExpr *Se = *E;
    return NCtx.castResult(E, self()->traverseSExpr(&Se, NCtx, K));
  }

  /// Special case for VarDecl, which bypasses the case statement
  MAPTYPE(RedT, VarDecl)
  traverse(VarDecl** E, const CtxT &Ctx, TraversalKind K = TRV_Normal) {
    CtxT NCtx = Ctx.sub(K);
    return NCtx.handleResult(E, self()->traverseVarDecl(*E, NCtx, K), K);
  }

  /// Special case for BasicBlock, which bypasses the case statement
  MAPTYPE(RedT, BasicBlock)
  traverse(BasicBlock** E, const CtxT &Ctx, TraversalKind K = TRV_Normal) {
    CtxT NCtx = Ctx.sub(K);
    return NCtx.handleResult(E, self()->traverseBasicBlock(*E, NCtx, K), K);
  }

public:
  /// Entry point for traversing an SExpr of unknown type.
  /// This will dispatch on the type of E, and call traverseX.
  R_SExpr traverseSExpr(SExpr** E, CtxT Ctx, TraversalKind K = TRV_Normal) {
    // Avoid infinite loops.
    // E->block() is true if E is an instruction in a basic block.
    // When traversing the instr from the basic block, then K == TRV_SubExpr.
    // Indirect references to the instruction will have K == TRV_Normal.
    if (K == TRV_Normal && (*E)->block())
      return self()->traverseWeakInstr(E, Ctx);
    return Ctx.handleResult(E, self()->traverseByCase(*E, Ctx, K), K);
  }

  /// Helper method to call traverseX(e) on the appropriate type.
  R_SExpr traverseByCase(SExpr *E, CtxT Ctx, TraversalKind K) {
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

  /// Entry point for traversing a weak (non-owning) ref to an instruction.
  MAPTYPE(RedT, SExpr) traverseWeakInstr(SExpr **E, CtxT Ctx) {
    return Ctx.handleResult(E, Ctx->reduceWeakInstr(*E), TRV_Weak);
  }

  /// Entry point for traversing a reference to a declared variable.
  MAPTYPE(RedT, VarDecl) traverseWeakVarDecl(VarDecl **E, CtxT Ctx) {
    return Ctx.handleResult(E, Ctx->reduceWeakVarDecl(E), TRV_Weak);
  }

  /// Entry point for traversing a branch target.
  MAPTYPE(RedT, BasicBlock) traverseWeakBasicBlock(BasicBlock **E, CtxT Ctx) {
    return Ctx.handleResult(E, Ctx->reduceWeakBasicBlock(*E), TRV_Weak);
  }
};


/// Implements a Context that simply forwards all enter/exit methods to the
/// Reducer object.  Used for Contexts that don't need to clone scopes.
/// RedT is the type of the Reducer.
template <class RedT>
class DefaultContext {
public:
  DefaultContext(RedT* RP) : RPtr(RP) { }
  DefaultContext(const DefaultContext<RedT>& c) = default;
  ~DefaultContext() { }

  /// Adjust the context when entering a particular kind of subexpression.
  DefaultContext<RedT> sub(TraversalKind K) const { return *this; }

  /// Handle the result of a traversal.  The default is just to return it.
  template<class T>
  MAPTYPE(RedT, T)
  handleResult(T** E, MAPTYPE(RedT, T) Result, TraversalKind K) {
    return Result;
  }

  /// Cast a result to the appropriate type.
  template<class T>
  MAPTYPE(RedT, T) castResult(T** E, MAPTYPE(RedT, SExpr) Result) {
    return static_cast<MAPTYPE(RedT, T)>(Result);
  }

  /// Forward the scope commands to the Reducer object.
  void enterScope(VarDecl* Orig, MAPTYPE(RedT, VarDecl) Nvd) {
    RPtr->enterScope(Orig, Nvd);
  }
  void exitScope(VarDecl* Orig) {
    RPtr->exitScope(Orig);
  }

  void enterBasicBlock(BasicBlock* Orig, MAPTYPE(RedT, BasicBlock) Nbb) {
    RPtr->enterBasicBlock(Orig, Nbb);
  }
  void exitBasicBlock(BasicBlock* Orig) {
    RPtr->exitBasicBlock(Orig);
  }

  void enterCFG(SCFG* Orig, MAPTYPE(RedT, SCFG) Ns) {
    RPtr->enterCFG(Orig, Ns);
  }
  void exitCFG(SCFG* Orig) {
    RPtr->exitCFG(Orig);
  }

  /// Used to invoke ReduceX methods on Reducer.
  RedT* operator->() { return RPtr; }

protected:
  RedT* get() const { return RPtr; }

private:
  RedT* RPtr;
};



template <class Self, class R>
class DefaultReducer {
public:
  typedef MAPTYPE(R,SExpr)      R_SExpr;
  typedef MAPTYPE(R,VarDecl)    R_VarDecl;
  typedef MAPTYPE(R,BasicBlock) R_BasicBlock;
  typedef MAPTYPE(R,SCFG)       R_SCFG;
  typedef MAPTYPE(R,Phi)        R_Phi;

  Self* self() { return static_cast<Self*>(this); }


  R_SExpr reduceSExpr(SExpr& Orig) {
    return self()->reduceNull();
  }
  R_SExpr reduceDefinition(SExpr& Orig) {
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reducePath(SExpr& Orig) {
    return self()->reduceSExpr(Orig);
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


  R_SExpr reduceWeakInstr(SExpr* E) {
    return self()->reduceNull();
  }
  R_VarDecl reduceWeakVarDecl(VarDecl *E) {
    return self()->reduceNull();
  }
  R_BasicBlock reduceWeakBasicBlock(BasicBlock *E) {
    return self()->reduceNull();
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

  R_VarDecl reduceVarDecl(VarDecl &Orig, R_SExpr E) {
    return self()->reduceInstruction(Orig);
  }
  R_SExpr reduceFunction(Function &Orig, R_VarDecl Nvd, R_SExpr E0) {
    return self()->reduceDefinition(Orig);
  }
  R_SExpr reduceSFunction(SFunction &Orig, R_VarDecl Nvd, R_SExpr E0) {
    return self()->reduceDefinition(Orig);
  }
  R_SExpr reduceCode(Code &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceDefinition(Orig);
  }
  R_SExpr reduceField(Field &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reduceDefinition(Orig);
  }

  R_SExpr reduceApply(Apply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reducePath(Orig);
  }
  R_SExpr reduceSApply(SApply &Orig, R_SExpr E0, R_SExpr E1) {
    return self()->reducePath(Orig);
  }
  R_SExpr reduceProject(Project &Orig, R_SExpr E0) {
    return self()->reducePath(Orig);
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
    return self()->reduceDefinition(Orig);
  }
  void reduceSCFGBlock(R_SCFG Scfg, unsigned i, R_BasicBlock B) { }
  R_SCFG reduceSCFG(R_SCFG Scfg) { return Scfg; }


  R_BasicBlock reduceBasicBlockBegin(BasicBlock &Orig) {
    return self()->reduceDefinition(Orig);
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
    return self()->reduceSExpr(Orig);
  }
  R_SExpr reduceWildcard(Wildcard &Orig) {
    return self()->reduceSExpr(Orig);
  }

  R_SExpr reduceIdentifier(Identifier &Orig) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceLet(Let &Orig, R_VarDecl Nvd, R_SExpr B) {
    return self()->reducePseudoTerm(Orig);
  }
  R_SExpr reduceIfThenElse(IfThenElse &Orig, R_SExpr C, R_SExpr T, R_SExpr E) {
    return self()->reducePseudoTerm(Orig);
  }
};


/// Provides default implementations of the enter/exit methods that do nothing.
/// R must provide the TypeMap.
template<class R>
class UnscopedReducer {
public:
  typedef MAPTYPE(R,SExpr)      R_SExpr;
  typedef MAPTYPE(R,VarDecl)    R_VarDecl;
  typedef MAPTYPE(R,BasicBlock) R_BasicBlock;
  typedef MAPTYPE(R,SCFG)       R_SCFG;

  void enterScope(VarDecl* Orig, R_VarDecl Nvd) { }
  void exitScope(VarDecl* Orig) { }

  void enterBasicBlock(BasicBlock* Orig, R_BasicBlock Nbb) { }
  void exitBasicBlock(BasicBlock* Orig) { }

  void enterCFG(SCFG* Orig, R_SCFG Ns) { }
  void exitCFG(SCFG* Orig) { }
};


/// Defines the TypeMap for VisitReducerBase
class VisitReducerMap {
public:
  /// A visitor maps all expression types to bool.
  template <class T> struct TypeMap { typedef bool Ty; };
};


/// Implements reduceX methods for a simple visitor.   A visitor "rewrites"
/// SExprs to booleans: it returns true on success, and false on failure.
class VisitReducerBase
    : public VisitReducerMap,
      public UnscopedReducer<VisitReducerMap>,
      public DefaultReducer<VisitReducerBase, VisitReducerMap> {
public:
  bool reduceNull()             { return true; }
  bool reduceSExpr(SExpr &Orig) { return true; }
};


/// Specialize Traversal to abort on failure.
template<class Self, class ReducerT>
class VisitTraversal : public Traversal<Self, ReducerT> {
public:
  typedef typename ReducerT::ContextT CtxT;

  VisitTraversal() : Success(true) { }

  Self *self() { return static_cast<Self *>(this); }

  /// Override traverse to set success, bail on failure, and ignore results
  bool traverseSExpr(SExpr **E, CtxT Ctx, TraversalKind K = TRV_Normal) {
    Success = Success && Traversal<Self, ReducerT>::traverseSExpr(E, Ctx, K);
    return Success;
  }

private:
  bool Success;
};


// Used by CopyReducerBase.  Most terms map to SExpr*.
template <class T> struct DefaultTypeMap { typedef SExpr* Ty; };

// These kinds of SExpr must map to the same kind.
// We define these here b/c template specializations cannot be class members.
template<> struct DefaultTypeMap<VarDecl>    { typedef VarDecl* Ty; };
template<> struct DefaultTypeMap<BasicBlock> { typedef BasicBlock* Ty; };
template<> struct DefaultTypeMap<SCFG>       { typedef SCFG* Ty; };

/// Defines the TypeMap for CopyReducerBase.
class SExprReducerMap {
public:
  // R_SExpr is the result type for a traversal.
  // A copy reducer returns a newly allocated term.
  template <class T> struct TypeMap : public DefaultTypeMap<T> { };
};


class CopyReducerBase
    : public SExprReducerMap,
      public UnscopedReducer<SExprReducerMap> {
public:
  CopyReducerBase() {}
  CopyReducerBase(MemRegionRef A) : Arena(A) { }

  void setArena(MemRegionRef A) { Arena = A; }

public:
  std::nullptr_t reduceNull() { return nullptr; }

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
  void reducePhiArg(Phi &Orig, Phi* Ph, unsigned i, SExpr* E) {
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
  switch (kind()) {
    case VK_Fun: {
      auto D = Vs.traverse(&Definition, Ctx, TRV_Type);
      return Ctx->reduceVarDecl(*this, D);
    }
    case VK_SFun: {
      // Don't traverse self-fun, since it points back to self.
      return Ctx->reduceVarDecl(*this, Ctx->reduceNull());
    }
    case VK_Let: {
      auto D = Vs.traverse(&Definition, Ctx, TRV_SubExpr);
      return Ctx->reduceVarDecl(*this, D);
    }
  }
}

template <class V>
MAPTYPE(V::RedT, Function) Function::traverse(V &Vs, typename V::CtxT Ctx) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverse(&VDecl, Ctx, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the function.
  Ctx.enterScope(VDecl, E0);
  auto E1 = Vs.traverse(&Body, Ctx, TRV_SubExpr);
  Ctx.exitScope(VDecl);
  return Ctx->reduceFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, SFunction) SFunction::traverse(V &Vs, typename V::CtxT Ctx) {
  // Traversing an self-definition is a no-op.
  auto E0 = Vs.traverse(&VDecl, Ctx, TRV_SubExpr);
  Ctx.enterScope(VDecl, E0);
  auto E1 = Vs.traverse(&Body, Ctx, TRV_SubExpr);
  Ctx.exitScope(VDecl);
  // The SFun constructor will set E0->Definition to E1.
  return Ctx->reduceSFunction(*this, E0, E1);
}

template <class V>
MAPTYPE(V::RedT, Code) Code::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nt = Vs.traverse(&ReturnType, Ctx, TRV_Type);
  auto Nb = Vs.traverse(&Body,       Ctx, TRV_Lazy);
  return Ctx->reduceCode(*this, Nt, Nb);
}

template <class V>
MAPTYPE(V::RedT, Field) Field::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nr = Vs.traverse(&Range, Ctx, TRV_Type);
  auto Nb = Vs.traverse(&Body,  Ctx, TRV_Lazy);
  return Ctx->reduceField(*this, Nr, Nb);
}

template <class V>
MAPTYPE(V::RedT, Apply) Apply::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nf = Vs.traverse(&Fun, Ctx);
  auto Na = Vs.traverse(&Arg, Ctx);
  return Ctx->reduceApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, SApply) SApply::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nf = Vs.traverse(&Sfun, Ctx);
  auto Na = Arg ? Vs.traverse(&Arg, Ctx) : Ctx->reduceNull();
  return Ctx->reduceSApply(*this, Nf, Na);
}

template <class V>
MAPTYPE(V::RedT, Project) Project::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nr = Vs.traverse(&Rec, Ctx);
  return Ctx->reduceProject(*this, Nr);
}

template <class V>
MAPTYPE(V::RedT, Call) Call::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nt = Vs.traverse(&Target, Ctx);
  return Ctx->reduceCall(*this, Nt);
}

template <class V>
MAPTYPE(V::RedT, Alloc) Alloc::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nd = Vs.traverse(&Dtype, Ctx, TRV_SubExpr);
  return Ctx->reduceAlloc(*this, Nd);
}

template <class V>
MAPTYPE(V::RedT, Load) Load::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Vs.traverse(&Ptr, Ctx);
  return Ctx->reduceLoad(*this, Np);
}

template <class V>
MAPTYPE(V::RedT, Store) Store::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Vs.traverse(&Dest,   Ctx);
  auto Nv = Vs.traverse(&Source, Ctx);
  return Ctx->reduceStore(*this, Np, Nv);
}

template <class V>
MAPTYPE(V::RedT, ArrayIndex) ArrayIndex::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Na = Vs.traverse(&Array, Ctx);
  auto Ni = Vs.traverse(&Index, Ctx);
  return Ctx->reduceArrayIndex(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, ArrayAdd) ArrayAdd::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Na = Vs.traverse(&Array, Ctx);
  auto Ni = Vs.traverse(&Index, Ctx);
  return Ctx->reduceArrayAdd(*this, Na, Ni);
}

template <class V>
MAPTYPE(V::RedT, UnaryOp) UnaryOp::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(&Expr0, Ctx);
  return Ctx->reduceUnaryOp(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BinaryOp) BinaryOp::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne0 = Vs.traverse(&Expr0, Ctx);
  auto Ne1 = Vs.traverse(&Expr1, Ctx);
  return Ctx->reduceBinaryOp(*this, Ne0, Ne1);
}

template <class V>
MAPTYPE(V::RedT, Cast) Cast::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(&Expr0, Ctx);
  return Ctx->reduceCast(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, Phi) Phi::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Np = Ctx->reducePhiBegin(*this);
  unsigned i = 0;
  for (auto *Va : Values) {
    Ctx->reducePhiArg(*this, Np, i, Vs.traverse(&Va, Ctx));
    ++i;
  }
  return Ctx->reducePhi(Np);
}

template <class V>
MAPTYPE(V::RedT, Goto) Goto::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ntb = Vs.traverseWeakBasicBlock(&TargetBlock, Ctx.sub(TRV_Weak));
  return Ctx->reduceGoto(*this, Ntb);
}

template <class V>
MAPTYPE(V::RedT, Branch) Branch::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nc  = Vs.traverse(&Condition, Ctx);
  auto Ntb = Vs.traverseWeakBasicBlock(&Branches[0], Ctx.sub(TRV_Weak));
  auto Nte = Vs.traverseWeakBasicBlock(&Branches[1], Ctx.sub(TRV_Weak));
  return Ctx->reduceBranch(*this, Nc, Ntb, Nte);
}

template <class V>
MAPTYPE(V::RedT, Return) Return::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ne = Vs.traverse(&Retval, Ctx);
  return Ctx->reduceReturn(*this, Ne);
}

template <class V>
MAPTYPE(V::RedT, BasicBlock) BasicBlock::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Nb = Ctx->reduceBasicBlockBegin(*this);
  Ctx.enterBasicBlock(this, Nb);
  unsigned i = 0;
  for (auto *A : Args) {
    // Use TRV_SubExpr to force traversal of arguments
    Ctx->reduceBasicBlockArg(Nb, i, Vs.traverse(&A, Ctx, TRV_SubExpr));
    ++i;
  }
  i = 0;
  for (auto *I : Instrs) {
    // Use TRV_SubExpr to force traversal of instructions
    Ctx->reduceBasicBlockInstr(Nb, i, Vs.traverse(&I, Ctx, TRV_SubExpr));
    ++i;
  }
  Ctx.exitBasicBlock(this);
  Ctx->reduceBasicBlockTerm(Nb, Vs.traverse(&TermInstr, Ctx, TRV_SubExpr));

  return Ctx->reduceBasicBlock(Nb);
}

template <class V>
MAPTYPE(V::RedT, SCFG) SCFG::traverse(V &Vs, typename V::CtxT Ctx) {
  auto Ns = Ctx->reduceSCFGBegin(*this);
  Ctx.enterCFG(this, Ns);

  unsigned i = 0;
  for (BasicBlock *&B : Blocks) {
    auto Nb = Vs.traverse(&B, Ctx, TRV_SubExpr);
    Ctx->reduceSCFGBlock(Ns, i, Nb);
    ++i;
  }
  Ctx.exitCFG(this);
  return Ctx->reduceSCFG(Ns);
}

template <class V>
MAPTYPE(V::RedT, Future) Future::traverse(V &Vs, typename V::CtxT Ctx) {
  assert(Result && "Cannot traverse Future that has not been forced.");
  return Vs.traverse(&Result, Ctx);
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
  auto Nc = Vs.traverse(&Condition, Ctx);
  auto Nt = Vs.traverse(&ThenExpr,  Ctx, TRV_Tail);
  auto Ne = Vs.traverse(&ElseExpr,  Ctx, TRV_Tail);
  return Ctx->reduceIfThenElse(*this, Nc, Nt, Ne);
}

template <class V>
MAPTYPE(V::RedT, Let) Let::traverse(V &Vs, typename V::CtxT Ctx) {
  // This is a variable declaration, so traverse the definition.
  auto E0 = Vs.traverse(&VDecl, Ctx, TRV_SubExpr);
  // Tell the rewriter to enter the scope of the let variable.
  Ctx.enterScope(VDecl, E0);
  auto E1 = Vs.traverse(&Body, Ctx, TRV_Tail);
  Ctx.exitScope(VDecl);
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
