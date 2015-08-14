//===- ClangTranslator.h ---------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#ifndef CLANG_ANALYSIS_TIL_CLANGTRANSLATOR_H
#define CLANG_ANALYSIS_TIL_CLANGTRANSLATOR_H

#include "clang/Analysis/Analyses/ThreadSafetyCommon.h"
#include "clang/Analysis/Til/ClangCFGWalker.h"
#include "clang/Analysis/Til/CFGBuilder.h"
#include "clang/Analysis/Til/TIL.h"
#include "clang/AST/ExprObjC.h"


namespace clang {
namespace tilcpp {

typedef clang::threadSafety::CapabilityExpr CapabilityExpr;

using namespace ohmu;


// Translate clang::Expr to til::SExpr.
class ClangTranslator {
public:
  /// \brief Encapsulates the lexical context of a function call.  The lexical
  /// context includes the arguments to the call, including the implicit object
  /// argument.  When an attribute containing a mutex expression is attached to
  /// a method, the expression may refer to formal parameters of the method.
  /// Actual arguments must be substituted for formal parameters to derive
  /// the appropriate mutex expression in the lexical context where the function
  /// is called.  PrevCtx holds the context in which the arguments themselves
  /// should be evaluated; multiple calling contexts can be chained together
  /// by the lock_returned attribute.
  struct CallingContext {
    CallingContext  *Prev;      // The previous context; or 0 if none.
    const NamedDecl *AttrDecl;  // The decl to which the attr is attached.
    const Expr *SelfArg;        // Implicit object argument -- e.g. 'this'
    unsigned NumArgs;           // Number of funArgs
    const Expr *const *FunArgs; // Function arguments
    bool SelfArrow;             // is Self referred to with -> or .?

    CallingContext(CallingContext *P, const NamedDecl *D = nullptr)
        : Prev(P), AttrDecl(D), SelfArg(nullptr),
          NumArgs(0), FunArgs(nullptr), SelfArrow(false)
    {}
  };

  // Translate a clang statement or expression to a TIL expression.
  // Also performs substitution of variables; Ctx provides the context.
  // Dispatches on the type of S.
  til::SExpr *translate(const Stmt *S, CallingContext *Ctx);

  CapabilityExpr translateAttrExpr(const Expr *AttrExp,
                                   const NamedDecl *D,
                                   const Expr *DeclExp,
                                   VarDecl *SelfDecl = nullptr);
  CapabilityExpr translateAttrExpr(const Expr *AttrExp,
                                   CallingContext *Ctx);

  void setCapabilityExprMode(bool b) { CapabilityExprMode = b; }
  void setSSAMode(bool b)            { SSAMode = b; }

  til::SExpr *topLevelSlot() { return TopLevelSlot; }

  void dumpTopLevelSlot();

  til::CFGBuilder& builder() { return Builder; }
  MemRegionRef&    arena()   { return Builder.arena(); }

protected:
  // Map from clang statements to ohmu SExprs.
  typedef llvm::DenseMap<const Stmt*, til::Instruction*> StatementMap;

  // Map from local variable declarations to ohmu Alloc instructions.
  typedef llvm::DenseMap<const ValueDecl*, til::SExpr*> LocalVarMap;

  // Map from basic block IDs in the clang CFG to ohmu basic blocks.
  typedef std::vector<til::BasicBlock*> BasicBlockMap;


  til::SExpr *lookupStmt(const Stmt *S) {
    auto It = SMap.find(S);
    if (It != SMap.end())
      return It->second;
    else
      return nullptr;
  }

  til::SExpr *lookupLocalVar(const ValueDecl *Vd) {
    auto It = LVarMap.find(Vd);
    if (It != LVarMap.end())
      return It->second;
    else
      return nullptr;
  }

  til::BasicBlock *lookupBlock(const CFGBlock *B) {
    return BMap[B->getBlockID()];
  }

  // Ensure that E has been added as an instruction to the basic block.
  void ensureAddInstr(til::SExpr *E) {
    if (!E || E->isTrivial())
      return;
    if (auto *I = dyn_cast_or_null<til::Instruction>(E)) {
      if (!I->block())
        Builder.addInstr(I);
    }
  }

  void insertStmt(const Stmt *S, til::Instruction *E) {
    SMap.insert(std::make_pair(S, E));
  }

  void insertLocalVar(const ValueDecl *Vd, til::SExpr* E) {
    LVarMap.insert(std::make_pair(Vd, E));
  }

  void insertBlock(const CFGBlock* Cb, til::BasicBlock *Ob) {
    BMap[Cb->getBlockID()] = Ob;
  }

  til::SExpr* translateClangType(QualType Qt, ASTContext& Ac);

private:
  til::SExpr *translateDeclRefExpr(const DeclRefExpr *Dre,
                                   CallingContext *Ctx);
  til::SExpr *translateCXXThisExpr(const CXXThisExpr *Te, CallingContext *Ctx);
  til::SExpr *translateMemberExpr(const MemberExpr *Me, CallingContext *Ctx);
  til::SExpr *translateCallExpr(const CallExpr *Ce, CallingContext *Ctx,
                                const Expr *SelfE = nullptr);
  til::SExpr *translateCXXMemberCallExpr(const CXXMemberCallExpr *Me,
                                         CallingContext *Ctx);
  til::SExpr *translateCXXOperatorCallExpr(const CXXOperatorCallExpr *Oce,
                                           CallingContext *Ctx);
  til::SExpr *translateUnaryIncDec(const UnaryOperator *Uo,
                                   til::TIL_BinaryOpcode Op,
                                   bool Post, CallingContext *Ctx);
  til::SExpr *translateUnaryOperator(const UnaryOperator *Uo,
                                     CallingContext *Ctx);
  til::SExpr *translateBinOp(til::TIL_BinaryOpcode Op,
                             const BinaryOperator *Bo,
                             CallingContext *Ctx, bool Reverse = false);
  til::SExpr *translateBinAssign(til::TIL_BinaryOpcode Op,
                                 const BinaryOperator *Bo,
                                 CallingContext *Ctx);
  til::SExpr *translateBinaryOperator(const BinaryOperator *Bo,
                                      CallingContext *Ctx);
  til::SExpr *translateCastExpr(const CastExpr *Ce, CallingContext *Ctx);
  til::SExpr *translateArraySubscriptExpr(const ArraySubscriptExpr *E,
                                          CallingContext *Ctx);
  til::SExpr *translateAbstractConditionalOperator(
      const AbstractConditionalOperator *C, CallingContext *Ctx);

  til::SExpr *translateDeclStmt(const DeclStmt *S, CallingContext *Ctx);


  til::SExpr* translateCharacterLiteral(const CharacterLiteral *L,
                                        CallingContext *Ctx);
  til::SExpr* translateCXXBoolLiteralExpr(const CXXBoolLiteralExpr *L,
                                          CallingContext *Ctx);
  til::SExpr* translateIntegerLiteral(const IntegerLiteral *L,
                                      CallingContext *Ctx);
  til::SExpr* translateFloatingLiteral(const FloatingLiteral *L,
                                       CallingContext *Ctx);
  til::SExpr* translateStringLiteral(const StringLiteral *L,
                                     CallingContext *Ctx);
  til::SExpr* translateObjCStringLiteral(const ObjCStringLiteral *L,
                                         CallingContext *Ctx);
  til::SExpr* translateCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *L,
                                             CallingContext *Ctx);
  til::SExpr* translateGNUNullExpr(const GNUNullExpr *L,
                                   CallingContext *Ctx);


  // We implement the CFGVisitor API
  friend class ClangCFGWalker;

  void enterCFG(CFG *Cfg, const NamedDecl *D, const CFGBlock *First);

  void enterCFGBlock(const CFGBlock *B) { }
  bool visitPredecessors() { return false; }
  void handlePredecessor(const CFGBlock *Pred) { }
  void handlePredecessorBackEdge(const CFGBlock *Pred) { }

  void enterCFGBlockBody(const CFGBlock *B);
  void handleStatement(const Stmt *S);
  void handleDestructorCall(const VarDecl *VD, const CXXDestructorDecl *DD);
  void exitCFGBlockBody(const CFGBlock *B);

  bool visitSuccessors() { return false; }
  void handleSuccessor(const CFGBlock *Succ) { }
  void handleSuccessorBackEdge(const CFGBlock *Succ) { }
  void exitCFGBlock(const CFGBlock *B) { }

  void exitCFG(const CFGBlock *Last);

public:
  ClangTranslator(MemRegionRef A)
      : Builder(A), CapabilityExprMode(false), SSAMode(true),
        SelfVar(nullptr), TopLevelSlot(nullptr), NumFunctionParams(0) {
    // FIXME: we don't always have a self-variable.
    auto* Svd = Builder.newVarDecl(til::VarDecl::VK_SFun, "this", nullptr);
    SelfVar = Builder.newVariable(Svd);
  }

private:
  til::CFGBuilder Builder;
  StatementMap    SMap;
  LocalVarMap     LVarMap;
  BasicBlockMap   BMap;

  // Set to true when parsing capability expressions, which get translated
  // inaccurately in order to hack around smart pointers etc.
  bool CapabilityExprMode;

  // Set to true to run SSA pass after CFG construction.
  bool SSAMode;

  til::Variable* SelfVar;       // Variable to use for 'this'.  May be null.
  til::SExpr*    TopLevelSlot;
  unsigned       NumFunctionParams;
};

}  // end namespace tilcpp
}  // end namespace clang

#endif  // CLANG_ANALYSIS_TIL_CLANGTRANSLATOR_H
