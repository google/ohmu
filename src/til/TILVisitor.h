//===- TILVisitor.h --------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the reducer interface so that every reduce method simply
// calls a corresponding visit method.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVISITOR_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVISITOR_H

#include "TIL.h"
#include "TILTraverse.h"

namespace ohmu {
namespace til  {


/// Defines the TypeMap for VisitReducer
class VisitReducerMap {
public:
  /// A visitor maps all expression types to bool.
  template <class T> struct TypeMap { typedef bool Ty; };
  typedef bool NullType;

  static bool reduceNull() { return true; }
};



/// Implements a post-order visitor, which implements  the reduceX method to
/// call visitX for each type.  This is more convenient for operations that
/// do not care about the results of reducing (visiting) subexpressions, and
/// thus wish to use a simpler interface.
template<class Self>
class Visitor : public Traversal<Self, VisitReducerMap>,
                public DefaultScopeHandler<VisitReducerMap> {
public:
  typedef Traversal<Self, VisitReducerMap> SuperTv;

  Self* self() { return static_cast<Self*>(this); }

  Visitor() : Success(true) { }

  bool visitSExpr(SExpr& Orig) {
    return true;
  }

  /// Override these methods to visit a particular kind of SExpr
#define TIL_OPCODE_DEF(X)                     \
  bool visit##X(X &Orig) {                    \
    return self()->visitSExpr(Orig);          \
  }
#include "TILOps.def"
#undef TIL_OPCODE_DEF

  template<class T>
  bool visitLiteralT(LiteralT<T> &Orig) {
    return self()->visitSExpr(Orig);
  }


  /// Abort traversal on failure.
  template <class T>
  MAPTYPE(VisitReducerMap, T) traverse(T* E, TraversalKind K) {
    Success = Success && SuperTv::traverse(E, K);
    return Success;
  }

  static bool visit(SExpr *E) {
    Self Visitor;
    return Visitor.traverseAll(E);
  }


  //--------------------------------------------------------------------
  // Reducer methods call the corresponding visit methods.
  //---------------------------------------------------------- ---------

  bool reduceWeak(Instruction* E) { return true; }
  bool reduceWeak(VarDecl *E)     { return true; }
  bool reduceWeak(BasicBlock *E)  { return true; }

  bool reduceVarDecl(VarDecl &Orig, bool E) {
    return self()->visitVarDecl(Orig);
  }
  bool reduceVarDeclLetrec(bool VD, bool E) { return VD; }

  bool reduceFunction(Function &Orig, bool Nvd, bool E0) {
    return self()->visitFunction(Orig);
  }
  bool reduceCode(Code &Orig, bool E0, bool E1) {
    return self()->visitCode(Orig);
  }
  bool reduceField(Field &Orig, bool E0, bool E1) {
    return self()->visitField(Orig);
  }
  bool reduceSlot(Slot &Orig, bool E0) {
    return self()->visitSlot(Orig);
  }

  Record* reduceRecordBegin(Record &Orig) { return &Orig; }
  void handleRecordSlot(Record* R, bool S) { }
  bool reduceRecordEnd(Record* R) {
    return self()->visitRecord(*R);
  }

  bool reduceScalarType(ScalarType &Orig) {
    return self()->visitScalarType(Orig);
  }

  bool reduceLiteral(Literal &Orig) {
    return self()->visitLiteral(Orig);
  }
  template<class T>
  bool reduceLiteralT(LiteralT<T> &Orig) {
    return self()->visitLiteralT(Orig);
  }
  bool reduceVariable(Variable &Orig, bool VD) {
    return self()->visitVariable(Orig);
  }

  bool reduceApply(Apply &Orig, bool E0, bool E1) {
    return self()->visitApply(Orig);
  }
  bool reduceProject(Project &Orig, bool E0) {
    return self()->visitProject(Orig);
  }
  bool reduceCall(Call &Orig, bool E0) {
    return self()->visitCall(Orig);
  }
  bool reduceAlloc(Alloc &Orig, bool E0) {
    return self()->visitAlloc(Orig);
  }
  bool reduceLoad(Load &Orig, bool E0) {
    return self()->visitLoad(Orig);
  }
  bool reduceStore(Store &Orig, bool E0, bool E1) {
    return self()->visitStore(Orig);
  }
  bool reduceArrayIndex(ArrayIndex &Orig, bool E0, bool E1) {
    return self()->visitArrayIndex(Orig);
  }
  bool reduceArrayAdd(ArrayAdd &Orig, bool E0, bool E1) {
    return self()->visitArrayAdd(Orig);
  }
  bool reduceUnaryOp(UnaryOp &Orig, bool E0) {
    return self()->visitUnaryOp(Orig);
  }
  bool reduceBinaryOp(BinaryOp &Orig, bool E0, bool E1) {
    return self()->visitBinaryOp(Orig);
  }
  bool reduceCast(Cast &Orig, bool E0) {
    return self()->visitCast(Orig);
  }
  bool reduceBranch(Branch &Orig, bool C, bool B0, bool B1) {
    return self()->visitBranch(Orig);
  }
  bool reduceReturn(Return &Orig, bool E) {
    return self()->visitReturn(Orig);
  }
  Goto* reduceGotoBegin(Goto &Orig, bool B) { return &Orig; }
  void  handlePhiArg(Phi &Orig, Goto* NG, bool Res) { }
  bool reduceGotoEnd(Goto *G) {
    return self()->visitGoto(*G);
  }
  bool reducePhi(Phi &Orig) {
    return self()->visitPhi(Orig);
  }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) { return &Orig; }
  void handleBBArg  (Phi &Orig,         bool Res) { }
  void handleBBInstr(Instruction &Orig, bool Res) { }
  bool reduceBasicBlockEnd(BasicBlock *B, bool Tm) {
    return self()->visitBasicBlock(*B);
  }

  SCFG* reduceSCFG_Begin(SCFG &Orig) { return &Orig; }
  void handleCFGBlock(BasicBlock &Orig,  bool Res) { }
  bool reduceSCFG_End(SCFG* Scfg) {
    return self()->visitSCFG(*Scfg);
  }

  bool reduceUndefined(Undefined &Orig) {
    return self()->visitUndefined(Orig);
  }
  bool reduceWildcard(Wildcard &Orig) {
    return self()->visitWildcard(Orig);
  }
  bool reduceIdentifier(Identifier &Orig) {
    return self()->visitIdentifier(Orig);
  }
  bool reduceLet(Let &Orig, bool Nvd, bool B) {
    return self()->visitLet(Orig);
  }
  bool reduceLetrec(Letrec &Orig, bool Nvd, bool B) {
    return self()->visitLetrec(Orig);
  }
  bool reduceIfThenElse(IfThenElse &Orig, bool C, bool T, bool E) {
    return self()->visitIfThenElse(Orig);
  }

private:
  bool Success;
};



} // end namespace til
} // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVISITOR_H
