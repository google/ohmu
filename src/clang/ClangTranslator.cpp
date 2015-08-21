//===- ClangTranslator.cpp -------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Til/ClangTranslator.h"
#include "clang/Analysis/Til/SSAPass.h"
#include "clang/Analysis/Til/TILPrettyPrint.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>


namespace clang {
namespace tilcpp {

static bool isCalleeArrow(const Expr *E) {
  const MemberExpr *ME = dyn_cast<MemberExpr>(E->IgnoreParenCasts());
  return ME ? ME->isArrow() : false;
}


static StringRef getStringRefFromString(MemRegionRef A, const std::string& S) {
  unsigned len = S.length();
  char* Cs = A.allocateT<char>(len+1);
  strncpy(Cs, S.c_str(), len);
  return StringRef(Cs, len);
}


static StringRef getDeclName(MemRegionRef A, const NamedDecl *D,
                             bool Qual = false) {
  if (Qual)
    return getStringRefFromString(A, D->getQualifiedNameAsString());
  if (!D->getIdentifier())
    return getStringRefFromString(A, D->getNameAsString());
  return D->getName();
}


StringRef ClangTranslator::getMangledValueName(const NamedDecl* Nd) {
  if (!isa<FunctionDecl>(Nd) && !isa<VarDecl>(Nd))
    return getDeclName(Builder.arena(), Nd, true);

  if (!Mangler) {
    // Grab ourselves a mangler on first use...
    Mangler.reset(Nd->getASTContext().createMangleContext());
  }

  std::string mangledName;
  llvm::raw_string_ostream Sos(mangledName);

  if (auto *Cd = dyn_cast<CXXConstructorDecl>(Nd))
    Mangler->mangleCXXCtor(Cd, Ctor_Base, Sos);
  else if (auto *Dd = dyn_cast<CXXDestructorDecl>(Nd))
    Mangler->mangleCXXDtor(Dd, Dtor_Base, Sos);
  else
    Mangler->mangleName(Nd, Sos);

  return getStringRefFromString(Builder.arena(), Sos.str());
}


StringRef ClangTranslator::getMangledTypeName(const Type* Ty,
                                              const NamedDecl *Nd) {
  if (!Mangler) {
    // Grab ourselves a mangler on first use...
    Mangler.reset(Nd->getASTContext().createMangleContext());
  }

  // Grab the generic version with no qualifiers
  QualType Qt(Ty, Qualifiers().getAsOpaqueValue());

  std::string mangledName;
  llvm::raw_string_ostream Sos(mangledName);
  Mangler->mangleTypeName(Qt, Sos);
  return getStringRefFromString(Builder.arena(), Sos.str());
}


til::Project* ClangTranslator::makeProjectFromDecl(til::SExpr* E,
                                                   const NamedDecl *D) {
  StringRef S = getMangledValueName(D);
  auto *P = Builder.newProject(E, S);
  P->setForeignSlotDecl(D);
  return P;
}




/// \brief Translate a clang expression in an attribute to a til::SExpr.
/// Constructs the context from D, DeclExp, and SelfDecl.
///
/// \param AttrExp The expression to translate.
/// \param D       The declaration to which the attribute is attached.
/// \param DeclExp An expression involving the Decl to which the attribute
///                is attached.  E.g. the call to a function.
CapabilityExpr ClangTranslator::translateAttrExpr(const Expr *AttrExp,
                                                  const NamedDecl *D,
                                                  const Expr *DeclExp,
                                                  VarDecl *SelfDecl) {
  // If we are processing a raw attribute expression, with no substitutions.
  if (!DeclExp)
    return translateAttrExpr(AttrExp, nullptr);

  CallingContext Ctx(nullptr, D);

  // Examine DeclExp to find SelfArg and FunArgs, which are used to substitute
  // for formal parameters when we call buildMutexID later.
  if (const MemberExpr *ME = dyn_cast<MemberExpr>(DeclExp)) {
    Ctx.SelfArg   = ME->getBase();
    Ctx.SelfArrow = ME->isArrow();
  } else if (const CXXMemberCallExpr *CE =
             dyn_cast<CXXMemberCallExpr>(DeclExp)) {
    Ctx.SelfArg   = CE->getImplicitObjectArgument();
    Ctx.SelfArrow = isCalleeArrow(CE->getCallee());
    Ctx.NumArgs   = CE->getNumArgs();
    Ctx.FunArgs   = CE->getArgs();
  } else if (const CallExpr *CE = dyn_cast<CallExpr>(DeclExp)) {
    Ctx.NumArgs = CE->getNumArgs();
    Ctx.FunArgs = CE->getArgs();
  } else if (const CXXConstructExpr *CE =
      dyn_cast<CXXConstructExpr>(DeclExp)) {
    Ctx.SelfArg = nullptr;  // Will be set below
    Ctx.NumArgs = CE->getNumArgs();
    Ctx.FunArgs = CE->getArgs();
  } else if (D && isa<CXXDestructorDecl>(D)) {
    // There's no such thing as a "destructor call" in the AST.
    Ctx.SelfArg = DeclExp;
  }

  // Hack to handle constructors, where self cannot be recovered from
  // the expression.
  if (SelfDecl && !Ctx.SelfArg) {
    DeclRefExpr SelfDRE(SelfDecl, false, SelfDecl->getType(), VK_LValue,
                        SelfDecl->getLocation());
    Ctx.SelfArg = &SelfDRE;

    // If the attribute has no arguments, then assume the argument is "this".
    if (!AttrExp)
      return translateAttrExpr(Ctx.SelfArg, nullptr);
    else  // For most attributes.
      return translateAttrExpr(AttrExp, &Ctx);
  }

  // If the attribute has no arguments, then assume the argument is "this".
  if (!AttrExp)
    return translateAttrExpr(Ctx.SelfArg, nullptr);
  else  // For most attributes.
    return translateAttrExpr(AttrExp, &Ctx);
}


/// \brief Translate a clang expression in an attribute to a til::SExpr.
// This assumes a CallingContext has already been created.
CapabilityExpr ClangTranslator::translateAttrExpr(const Expr *AttrExp,
                                                  CallingContext *Ctx) {
  if (!AttrExp)
    return CapabilityExpr(nullptr, false);

  if (auto* SLit = dyn_cast<StringLiteral>(AttrExp)) {
    if (SLit->getString() == StringRef("*"))
      // The "*" expr is a universal lock, which essentially turns off
      // checks until it is removed from the lockset.
      return CapabilityExpr(Builder.newWildcard(), false);
    else
      // Ignore other string literals for now.
      return CapabilityExpr(nullptr, false);
  }

  bool Neg = false;
  if (auto *OE = dyn_cast<CXXOperatorCallExpr>(AttrExp)) {
    if (OE->getOperator() == OO_Exclaim) {
      Neg = true;
      AttrExp = OE->getArg(0);
    }
  }
  else if (auto *UO = dyn_cast<UnaryOperator>(AttrExp)) {
    if (UO->getOpcode() == UO_LNot) {
      Neg = true;
      AttrExp = UO->getSubExpr();
    }
  }

  til::SExpr *E = translate(AttrExp, Ctx);

  // Trap mutex expressions like nullptr, or 0.
  // Any literal value is nonsense.
  if (!E || isa<til::Literal>(E))
    return CapabilityExpr(nullptr, false);

  // Hack to deal with smart pointers -- strip off top-level pointer casts.
  if (auto *CE = dyn_cast_or_null<til::Cast>(E)) {
    if (CE->castOpcode() == til::CAST_objToPtr)
      return CapabilityExpr(CE->expr(), Neg);
  }
  return CapabilityExpr(E, Neg);
}



static til::BaseType getBaseTypeFromClangType(QualType Qt) {
  if (Qt->hasPointerRepresentation())
    return til::BaseType::getBaseType<void*>();

  // TODO: this doesn't work for cross-compilers!
  if (auto *Bt = Qt->getAs<BuiltinType>()) {
    switch (Bt->getKind()) {
      case BuiltinType::Void:
        return til::BaseType::getBaseType<void>();
      case BuiltinType::Bool:
        return til::BaseType::getBaseType<bool>();
      case BuiltinType::Char_U:
      case BuiltinType::UChar:
        return til::BaseType::getBaseType<unsigned char>();
      case BuiltinType::Char_S:
      case BuiltinType::SChar:
        return til::BaseType::getBaseType<signed char>();

      case BuiltinType::Short:
        return til::BaseType::getBaseType<short>();
      case BuiltinType::Int:
        return til::BaseType::getBaseType<int>();
      case BuiltinType::Long:
        return til::BaseType::getBaseType<long>();

      case BuiltinType::UShort:
        return til::BaseType::getBaseType<unsigned short>();
      case BuiltinType::UInt:
        return til::BaseType::getBaseType<unsigned int>();
      case BuiltinType::ULong:
        return til::BaseType::getBaseType<unsigned long>();

      case BuiltinType::Float:
        return til::BaseType::getBaseType<float>();
      case BuiltinType::Double:
        return til::BaseType::getBaseType<double>();

      default:
        break;
    }
  }

  return til::BaseType(til::BaseType::BT_Void, til::BaseType::ST_0, 0);
}


static void setBaseTypeFromClangExpr(til::Instruction* I, const Expr *E) {
  I->setBaseType( getBaseTypeFromClangType(E->getType()) );
}


til::SExpr* ClangTranslator::translateClangType(QualType Qt, bool LValue) {
  if (Qt->isVoidType()) {
    auto *Vt = Builder.newScalarType( til::BaseType::getBaseType<void>() );
    if (!LValue)
      return Vt;
    else
      return Builder.newScalarType( til::BaseType::getBaseType<void*>() );
  }

  const Type *Ty = Qt.getTypePtr();

  if (isa<BuiltinType>(Ty)) {
    // A scalar (e.g. int) which is stored in a register is just a scalar.
    // However, a scalar which is stored in memory (as a slot or array element)
    // must be a field, so that it can be the target of store instructions.
    auto *Et = Builder.newScalarType( getBaseTypeFromClangType(Qt) );
    if (!LValue)
      return Et;
    else
      return Builder.newField(Et, nullptr);
  }

  if (auto* Ety = dyn_cast<EnumType>(Ty)) {
    auto* Ed = Ety->getDecl();
    StringRef S = getMangledTypeName(Ety, Ed);
    auto* Et = Builder.newProject(nullptr, S);
    Et->setForeignSlotDecl(Ed);

    if (!LValue)
      return Et;
    else
      return Builder.newField(Et, nullptr);
  }

  if (auto *Rty = dyn_cast<RecordType>(Ty)) {
    // Note, records are always passed by reference, so the following types
    // are the same:
    //   void f(Foo x)
    //   void f(Foo &x)
    //   void f(Foo *x)
    // The only difference is whether the caller has to create a copy.
    // TODO: return an owned type if !LValue

    RecordDecl* Rd = Rty->getDecl();
    StringRef S = getMangledTypeName(Rty, Rd);
    auto* P = Builder.newProject(nullptr, S);
    P->setForeignSlotDecl(Rd);
    return P;
  }

  if (auto *Tmty = dyn_cast<TemplateSpecializationType>(Ty)) {
    // Non-dependent specializations are always sugar, so we only worry about
    // sugared types.
    if (Tmty->isSugared())
      return translateClangType(Tmty->desugar(), LValue);
  }

  if (auto *Pty = dyn_cast<PointerType>(Ty)) {
    // Ohmu doesn't have a type that corresponds to "pointer to T".
    // PValues (e.g. records, functions, or fields) are pointers by
    // default, much like reference types in Java, while scalars are not.
    // If InMemory is true, then the recursive call will automatically "box"
    // non-pointer values into pointer types.
    auto *Et = translateClangType(Pty->getPointeeType(), true);
    if (!LValue)
      return Et;
    else
      return Builder.newField(Et, nullptr);
  }

  if (auto* Pty = dyn_cast<ReferenceType>(Ty)) {
    auto *Et = translateClangType(Pty->getPointeeType(), true);
    if (!LValue)
      return Et;
    else
      return Builder.newField(Et, nullptr);
  }

  if (auto* Tdty = dyn_cast<TypedefType>(Ty)) {
    return translateClangType(Tdty->desugar(), LValue);
  }

  if (auto* Pt = dyn_cast<ParenType>(Ty)) {
    return translateClangType(Pt->desugar(), LValue);
  }

  // For debugging
  // Ty->dump();

  return Builder.newUndefined();
}


til::SExpr *ClangTranslator::translate(const Stmt *S, CallingContext *Ctx) {
  if (!S)
    return nullptr;

  // Check if S has already been translated and cached.
  // This handles the lookup of SSA names for DeclRefExprs here.
  if (auto *E = lookupStmt(S))
    return E;

  til::SExpr* Res = nullptr;

  switch (S->getStmtClass()) {
  // Basic expressions
  case Stmt::DeclRefExprClass:
    Res = translateDeclRefExpr(cast<DeclRefExpr>(S), Ctx);
    break;
  case Stmt::CXXThisExprClass:
    Res = translateCXXThisExpr(cast<CXXThisExpr>(S), Ctx);
    break;
  case Stmt::MemberExprClass:
    Res = translateMemberExpr(cast<MemberExpr>(S), Ctx);
    break;
  case Stmt::CallExprClass:
    Res = translateCallExpr(cast<CallExpr>(S), Ctx);
    break;
  case Stmt::CXXMemberCallExprClass:
    Res = translateCXXMemberCallExpr(cast<CXXMemberCallExpr>(S), Ctx);
    break;
  case Stmt::CXXOperatorCallExprClass:
    Res = translateCXXOperatorCallExpr(cast<CXXOperatorCallExpr>(S), Ctx);
    break;
  case Stmt::UnaryOperatorClass:
    Res = translateUnaryOperator(cast<UnaryOperator>(S), Ctx);
    break;
  case Stmt::BinaryOperatorClass:
  case Stmt::CompoundAssignOperatorClass:
    Res = translateBinaryOperator(cast<BinaryOperator>(S), Ctx);
    break;
  case Stmt::ArraySubscriptExprClass:
    Res = translateArraySubscriptExpr(cast<ArraySubscriptExpr>(S), Ctx);
    break;
  case Stmt::ConditionalOperatorClass:
    Res = translateAbstractConditionalOperator(
        cast<ConditionalOperator>(S), Ctx);
    break;
  case Stmt::BinaryConditionalOperatorClass:
    Res = translateAbstractConditionalOperator(
        cast<BinaryConditionalOperator>(S), Ctx);
    break;

  // We treat these as no-ops
  case Stmt::ParenExprClass:
    Res = translate(cast<ParenExpr>(S)->getSubExpr(), Ctx);
    break;
  case Stmt::ExprWithCleanupsClass:
    Res = translate(cast<ExprWithCleanups>(S)->getSubExpr(), Ctx);
    break;
  case Stmt::CXXBindTemporaryExprClass:
    Res = translate(cast<CXXBindTemporaryExpr>(S)->getSubExpr(), Ctx);
    break;

  // Literals of various kinds
  case Stmt::CharacterLiteralClass:
    Res = translateCharacterLiteral(cast<CharacterLiteral>(S), Ctx);
    break;
  case Stmt::CXXBoolLiteralExprClass:
    Res = translateCXXBoolLiteralExpr(cast<CXXBoolLiteralExpr>(S), Ctx);
    break;
  case Stmt::FloatingLiteralClass:
    Res = translateFloatingLiteral(cast<FloatingLiteral>(S), Ctx);
    break;
  case Stmt::IntegerLiteralClass:
    Res = translateIntegerLiteral(cast<IntegerLiteral>(S), Ctx);
    break;
  case Stmt::ImaginaryLiteralClass:
    Res = Builder.newUndefined();
    break;
  case Stmt::StringLiteralClass:
    Res = translateStringLiteral(cast<StringLiteral>(S), Ctx);
    break;
  case Stmt::ObjCStringLiteralClass:
    Res = translateObjCStringLiteral(cast<ObjCStringLiteral>(S), Ctx);
    break;
  case Stmt::CXXNullPtrLiteralExprClass:
    Res = translateCXXNullPtrLiteralExpr(cast<CXXNullPtrLiteralExpr>(S), Ctx);
    break;
  case Stmt::GNUNullExprClass:
    Res = translateGNUNullExpr(cast<GNUNullExpr>(S), Ctx);
    break;

  case Stmt::CXXNewExprClass:
    Res = translateCXXNewExpr(cast<CXXNewExpr>(S), Ctx);
    break;
  case Stmt::CXXDeleteExprClass:
    Res = translateCXXDeleteExpr(cast<CXXDeleteExpr>(S), Ctx);
    break;
  case Stmt::DeclStmtClass:
    Res = translateDeclStmt(cast<DeclStmt>(S), Ctx);
    break;
  default: {
    if (const CastExpr *CE = dyn_cast<CastExpr>(S))
      Res = translateCastExpr(CE, Ctx);
    break;
  }
  }

  // For debugging:
  // S->dump();

  if (!Res)
    Res = Builder.newUndefined();

  // If we're in the default scope, then update the statement map
  auto *I = dyn_cast_or_null<til::Instruction>(Res);
  if (I && !CapabilityExprMode && !Ctx)
    insertStmt(S, I);

  return Res;
}



til::SExpr *ClangTranslator::translateDeclRefExpr(const DeclRefExpr *Dre,
                                                  CallingContext *Ctx) {
  const ValueDecl *Vd = cast<ValueDecl>(Dre->getDecl()->getCanonicalDecl());

  if (auto *E = lookupLocalVar(Vd))
    return E;

  // Function parameters require substitution and/or renaming.
  if (const ParmVarDecl *Pv = dyn_cast_or_null<ParmVarDecl>(Vd)) {
    const FunctionDecl *Fd =
        cast<FunctionDecl>(Pv->getDeclContext())->getCanonicalDecl();
    unsigned I = Pv->getFunctionScopeIndex();

    if (Ctx && Ctx->FunArgs && Fd == Ctx->AttrDecl->getCanonicalDecl()) {
      // Substitute call arguments for references to function parameters
      assert(I < Ctx->NumArgs);
      return translate(Ctx->FunArgs[I], Ctx->Prev);
    }
    // Map the param back to the param of the original function declaration
    // for consistent comparisons.
    Vd = Fd->getParamDecl(I);
  }

  // Treat global variables as projections from the global scope
  return makeProjectFromDecl(nullptr, Vd);
}


til::SExpr *ClangTranslator::translateCXXThisExpr(const CXXThisExpr *TE,
                                                  CallingContext *Ctx) {
  // Substitute for 'this'
  if (Ctx && Ctx->SelfArg)
    return translate(Ctx->SelfArg, Ctx->Prev);
  assert(SelfVar && "We have no variable for 'this'!");
  return SelfVar;
}



static bool hasCppPointerType(const til::SExpr *E) {
  if (auto* L = dyn_cast<til::Load>(E)) {
    E = L->pointer();
  }
  if (auto* P = dyn_cast<til::Project>(E)) {
    auto *Vd = threadSafety::getClangSlotDecl(P);
    if (Vd && Vd->getType()->isPointerType())
      return true;
  }
  else if (auto *C = dyn_cast<til::Cast>(E)) {
    return C->castOpcode() == til::CAST_objToPtr;
  }
  return false;
}


// Grab the very first declaration of virtual method D
static const CXXMethodDecl *getFirstVirtualDecl(const CXXMethodDecl *D) {
  while (true) {
    D = D->getCanonicalDecl();
    CXXMethodDecl::method_iterator I = D->begin_overridden_methods(),
                                   E = D->end_overridden_methods();
    if (I == E)
      return D;  // Method does not override anything
    D = *I;      // FIXME: this does not work with multiple inheritance.
  }
  return nullptr;
}


til::SExpr *ClangTranslator::translateMemberExpr(const MemberExpr *Me,
                                                 CallingContext *Ctx) {
  // Create a self-application for the base expr
  til::SExpr *Be = translate(Me->getBase(), Ctx);
  til::SExpr *E  = Builder.newApply(Be, nullptr, til::Apply::FAK_SApply);

  const ValueDecl *D = Me->getMemberDecl();
  if (auto *Vd = dyn_cast<CXXMethodDecl>(D))
    D = getFirstVirtualDecl(Vd);

  auto* P = makeProjectFromDecl(E, D);

  if (hasCppPointerType(Be))
    P->setArrow(true);
  return P;
}


til::SExpr *ClangTranslator::translateCallExpr(const CallExpr *Ce,
                                               CallingContext *Ctx,
                                               const Expr *SelfE) {
  if (CapabilityExprMode) {
    // Handle LOCK_RETURNED
    const FunctionDecl *Fd = Ce->getDirectCallee()->getMostRecentDecl();
    if (LockReturnedAttr* At = Fd->getAttr<LockReturnedAttr>()) {
      CallingContext LRCallCtx(Ctx);
      LRCallCtx.AttrDecl = Ce->getDirectCallee();
      LRCallCtx.SelfArg  = SelfE;
      LRCallCtx.NumArgs  = Ce->getNumArgs();
      LRCallCtx.FunArgs  = Ce->getArgs();
      return const_cast<til::SExpr*>(
          translateAttrExpr(At->getArg(), &LRCallCtx).sexpr());
    }
  }

  til::SExpr *E = translate(Ce->getCallee(), Ctx);

  for (const auto *Arg : Ce->arguments()) {
    til::SExpr *A = translate(Arg, Ctx);
    E = Builder.newApply(E, A);
  }
  return Builder.newCall(E);
}


til::SExpr *ClangTranslator::translateCXXMemberCallExpr(
    const CXXMemberCallExpr *Me, CallingContext *Ctx)
{
  if (CapabilityExprMode) {
    // Ignore calls to get() on smart pointers.
    if (Me->getMethodDecl()->getNameAsString() == "get" &&
        Me->getNumArgs() == 0) {
      auto *E = translate(Me->getImplicitObjectArgument(), Ctx);
      return Builder.newCast(til::CAST_objToPtr, E);
      // return E;
    }
  }

  return translateCallExpr(cast<CallExpr>(Me), Ctx,
                           Me->getImplicitObjectArgument());
}


til::SExpr *ClangTranslator::translateCXXOperatorCallExpr(
    const CXXOperatorCallExpr *Oce, CallingContext *Ctx)
{
  if (CapabilityExprMode) {
    // Ignore operator * and operator -> on smart pointers.
    OverloadedOperatorKind k = Oce->getOperator();
    if (k == OO_Star || k == OO_Arrow) {
      auto *E = translate(Oce->getArg(0), Ctx);
      return Builder.newCast(til::CAST_objToPtr, E);
    }
  }
  return translateCallExpr(cast<CallExpr>(Oce), Ctx);
}



// Return a literal 1 of the given type.
til::Instruction *getLiteralOne(til::BaseType Bt, til::CFGBuilder& Builder,
                                bool Neg) {
  switch (Bt.Size) {
    case til::BaseType::ST_32: {
      switch (Bt.Base) {
        case til::BaseType::BT_Int:
          return Builder.newLiteralT<int32_t>(1);
        case til::BaseType::BT_UnsignedInt:
          return Builder.newLiteralT<uint32_t>(1);
        default: break;
      }
    }
    case til::BaseType::ST_64: {
      switch (Bt.Base) {
        case til::BaseType::BT_Int:
          return Builder.newLiteralT<int64_t>(1);
        case til::BaseType::BT_UnsignedInt:
          return Builder.newLiteralT<uint64_t>(1);
        default: break;
      }
    }
    default: break;
  }

  // This case occurs for pointer types
  if (Neg)
    return Builder.newLiteralT<int32_t>(-1);
  else
    return Builder.newLiteralT<int32_t>(1);
}



til::SExpr *ClangTranslator::translateUnaryIncDec(const UnaryOperator *Uo,
                                                  til::TIL_BinaryOpcode Op,
                                                  bool Post,
                                                  CallingContext *Ctx) {
  til::BaseType Bt = getBaseTypeFromClangType(Uo->getType());

  auto* E0 = translate(Uo->getSubExpr(), Ctx);
  // E0 appears in two places, which could create an illegal DAG.
  ensureAddInstr(E0);

  auto* Ld = Builder.newLoad(E0);
  Ld->setBaseType(Bt);

  til::Instruction* Be;

  // Pointer arithmetic
  if (Ld->baseType().isPointer()) {
    til::Instruction* One;
    if (Op == til::BOP_Sub)
      One = getLiteralOne(Bt, Builder, true);
    else
      One = getLiteralOne(Bt, Builder, false);

    Be = Builder.newArrayAdd(Ld, One);
    Be->setBaseType( til::BaseType::getBaseType<void*>() );
  }
  else {
    auto* One = getLiteralOne(Bt, Builder, false);
    Be = Builder.newBinaryOp(Op, Ld, One);
    Be->setBaseType(Bt);
  }

  Builder.newStore(E0, Be);

  if (Post)
    return Ld;
  else
    return E0;  // return reference
}



til::SExpr *ClangTranslator::translateUnaryOperator(const UnaryOperator *Uo,
                                                    CallingContext *Ctx) {
  switch (Uo->getOpcode()) {
  case UO_PostInc: return translateUnaryIncDec(Uo, til::BOP_Add, true,  Ctx);
  case UO_PostDec: return translateUnaryIncDec(Uo, til::BOP_Sub, true,  Ctx);
  case UO_PreInc:  return translateUnaryIncDec(Uo, til::BOP_Add, false, Ctx);
  case UO_PreDec:  return translateUnaryIncDec(Uo, til::BOP_Sub, false, Ctx);

  case UO_AddrOf: {
    if (CapabilityExprMode) {
      // interpret &Graph::mu_ as an existential.
      if (DeclRefExpr* Dre = dyn_cast<DeclRefExpr>(Uo->getSubExpr())) {
        ValueDecl *D = Dre->getDecl();
        if (D->isCXXInstanceMember()) {
          // This is a pointer-to-member expression, e.g. &MyClass::mu_.
          // We interpret this syntax specially, as a wildcard.
          auto *W = Builder.newWildcard();
          StringRef Nm = getDeclName(Builder.arena(), D, true);
          auto *P = Builder.newProject(W, Nm);
          P->setForeignSlotDecl(D);
          return P;
        }
      }
    }

    // otherwise, & is a no-op
    return translate(Uo->getSubExpr(), Ctx);
  }

  // We treat these as no-ops
  case UO_Deref:
  case UO_Plus:
    return translate(Uo->getSubExpr(), Ctx);

  case UO_Minus: {
    auto *I =  Builder.newUnaryOp(til::UOP_Negative,
                                  translate(Uo->getSubExpr(), Ctx));
    setBaseTypeFromClangExpr(I, Uo);
    return I;
  }
  case UO_Not: {
    auto *I = Builder.newUnaryOp(til::UOP_BitNot,
                                 translate(Uo->getSubExpr(), Ctx));
    setBaseTypeFromClangExpr(I, Uo);
    return I;
  }
  case UO_LNot: {
    auto *I = Builder.newUnaryOp(til::UOP_LogicNot,
                                 translate(Uo->getSubExpr(), Ctx));
    setBaseTypeFromClangExpr(I, Uo);
    return I;
  }

  // Currently unsupported
  case UO_Real:
  case UO_Imag:
  case UO_Extension:
    return Builder.newUndefined();
  }
  return Builder.newUndefined();
}



til::Instruction* makeBinaryOp(til::CFGBuilder& Builder,
                               til::TIL_BinaryOpcode Op,
                               til::SExpr *E0,
                               til::SExpr *E1) {
  auto* I0 = dyn_cast_or_null<til::Instruction>(E0);
  auto* I1 = dyn_cast_or_null<til::Instruction>(E1);

  // Handle pointer arithmetic
  if (Op == til::BOP_Add) {
    if (I0 && I0->baseType().isPointer()) {
      auto* Ebop = Builder.newArrayAdd(E0, E1);
      Ebop->setBaseType( til::BaseType::getBaseType<void*>() );
      return Ebop;
    }
    else if (I1 && I1->baseType().isPointer()) {
      auto* Ebop = Builder.newArrayAdd(E1, E0);
      Ebop->setBaseType( til::BaseType::getBaseType<void*>() );
      return Ebop;
    }
  }

  if (Op == til::BOP_Sub) {
    if (I0 && I0->baseType().isPointer()) {
      auto* SE1 = Builder.newUnaryOp(til::UOP_Negative, E1);
      if (I1)
        SE1->setBaseType(I1->baseType());
      auto* Ebop = Builder.newArrayAdd(E0, SE1);
      Ebop->setBaseType( til::BaseType::getBaseType<void*>() );
      return Ebop;
    }
  }

  return Builder.newBinaryOp(Op, E0, E1);
}



til::SExpr *ClangTranslator::translateBinOp(til::TIL_BinaryOpcode Op,
                                            const BinaryOperator *Bo,
                                            CallingContext *Ctx,
                                            bool Reverse) {
   til::SExpr *E0 = translate(Bo->getLHS(), Ctx);
   til::SExpr *E1 = translate(Bo->getRHS(), Ctx);

   til::Instruction* Ebop;
   if (Reverse)
     Ebop = Builder.newBinaryOp(Op, E1, E0);   // Only for > or >=
   else
     Ebop = makeBinaryOp(Builder, Op, E0, E1);

   setBaseTypeFromClangExpr(Ebop, Bo);
   return Ebop;
}


til::SExpr *ClangTranslator::translateBinAssign(til::TIL_BinaryOpcode Op,
                                                const BinaryOperator *Bo,
                                                CallingContext *Ctx) {
  til::SExpr *E0 = translate(Bo->getLHS(), Ctx);
  // E0 may appear in two places, which could create an illegal DAG.
  ensureAddInstr(E0);

  til::SExpr *E1 = translate(Bo->getRHS(), Ctx);

  if (Op != til::BOP_Eq) {
    auto* Ld = Builder.newLoad(E0);
    setBaseTypeFromClangExpr(Ld, Bo->getLHS());

    auto* Bop = makeBinaryOp(Builder, Op, Ld, E1);
    setBaseTypeFromClangExpr(Bop, Bo);
    E1 = Bop;
  }
  Builder.newStore(E0, E1);
  return E0;
}


til::SExpr *ClangTranslator::translateBinaryOperator(const BinaryOperator *Bo,
                                                     CallingContext *Ctx) {
  switch (Bo->getOpcode()) {
  case BO_PtrMemD:
  case BO_PtrMemI:
    return Builder.newUndefined();

  case BO_Mul:  return translateBinOp(til::BOP_Mul, Bo, Ctx);
  case BO_Div:  return translateBinOp(til::BOP_Div, Bo, Ctx);
  case BO_Rem:  return translateBinOp(til::BOP_Rem, Bo, Ctx);
  case BO_Add:  return translateBinOp(til::BOP_Add, Bo, Ctx);
  case BO_Sub:  return translateBinOp(til::BOP_Sub, Bo, Ctx);
  case BO_Shl:  return translateBinOp(til::BOP_Shl, Bo, Ctx);
  case BO_Shr:  return translateBinOp(til::BOP_Shr, Bo, Ctx);
  case BO_LT:   return translateBinOp(til::BOP_Lt,  Bo, Ctx);
  case BO_GT:   return translateBinOp(til::BOP_Lt,  Bo, Ctx, true);
  case BO_LE:   return translateBinOp(til::BOP_Leq, Bo, Ctx);
  case BO_GE:   return translateBinOp(til::BOP_Leq, Bo, Ctx, true);
  case BO_EQ:   return translateBinOp(til::BOP_Eq,  Bo, Ctx);
  case BO_NE:   return translateBinOp(til::BOP_Neq, Bo, Ctx);
  case BO_And:  return translateBinOp(til::BOP_BitAnd,   Bo, Ctx);
  case BO_Xor:  return translateBinOp(til::BOP_BitXor,   Bo, Ctx);
  case BO_Or:   return translateBinOp(til::BOP_BitOr,    Bo, Ctx);
  case BO_LAnd: return translateBinOp(til::BOP_LogicAnd, Bo, Ctx);
  case BO_LOr:  return translateBinOp(til::BOP_LogicOr,  Bo, Ctx);

  case BO_Assign:    return translateBinAssign(til::BOP_Eq,  Bo, Ctx);
  case BO_MulAssign: return translateBinAssign(til::BOP_Mul, Bo, Ctx);
  case BO_DivAssign: return translateBinAssign(til::BOP_Div, Bo, Ctx);
  case BO_RemAssign: return translateBinAssign(til::BOP_Rem, Bo, Ctx);
  case BO_AddAssign: return translateBinAssign(til::BOP_Add, Bo, Ctx);
  case BO_SubAssign: return translateBinAssign(til::BOP_Sub, Bo, Ctx);
  case BO_ShlAssign: return translateBinAssign(til::BOP_Shl, Bo, Ctx);
  case BO_ShrAssign: return translateBinAssign(til::BOP_Shr, Bo, Ctx);
  case BO_AndAssign: return translateBinAssign(til::BOP_BitAnd, Bo, Ctx);
  case BO_XorAssign: return translateBinAssign(til::BOP_BitXor, Bo, Ctx);
  case BO_OrAssign:  return translateBinAssign(til::BOP_BitOr,  Bo, Ctx);

  case BO_Comma:
    // The clang CFG should have already processed both sides.
    return translate(Bo->getRHS(), Ctx);
  }
  return Builder.newUndefined();
}


til::SExpr *ClangTranslator::translateCastExpr(const CastExpr *CE,
                                               CallingContext *Ctx) {
  clang::CastKind K = CE->getCastKind();
  switch (K) {
  case CK_LValueToRValue: {
    if (CapabilityExprMode) {
      // Ignore loads when translating attribute expressions.
      // TODO: we should only ignore when substituting for parameters...
      return translate(CE->getSubExpr(), Ctx);
    }
    auto *E0 = translate(CE->getSubExpr(), Ctx);
    auto *Ld = Builder.newLoad(E0);
    setBaseTypeFromClangExpr(Ld, CE);
    return Ld;
  }
  case CK_NoOp:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay: {
    // These map to a no-op.
    auto *E0 = translate(CE->getSubExpr(), Ctx);
    return E0;
  }
  default: {
    // FIXME: handle different kinds of casts.
    auto *E0 = translate(CE->getSubExpr(), Ctx);
    if (CapabilityExprMode)
      return E0;
    auto* Re = Builder.newCast(til::CAST_none, E0);
    setBaseTypeFromClangExpr(Re, CE);
    return Re;
  }
  }
}


til::SExpr *
ClangTranslator::translateArraySubscriptExpr(const ArraySubscriptExpr *E,
                                          CallingContext *Ctx) {
  til::SExpr *E0 = translate(E->getBase(), Ctx);
  til::SExpr *E1 = translate(E->getIdx(), Ctx);
  return Builder.newArrayIndex(E0, E1);
}


til::SExpr *
ClangTranslator::translateAbstractConditionalOperator(
    const AbstractConditionalOperator *CO, CallingContext *Ctx) {
  auto *C = translate(CO->getCond(), Ctx);
  auto *T = translate(CO->getTrueExpr(), Ctx);
  auto *E = translate(CO->getFalseExpr(), Ctx);
  return Builder.newIfThenElse(C, T, E);
}


til::SExpr *ClangTranslator::translateCXXNewExpr(const CXXNewExpr *Ne,
                                                 CallingContext *Ctx) {
  QualType Qt = Ne->getAllocatedType();
  auto *Typ = translateClangType(Qt);
  auto *Alc = Builder.newAlloc(Typ, til::Alloc::AK_Heap);

  // TODO: handle arrays, operator new, and placement args.
  if (auto* Ein = Ne->getInitializer()) {
    if (auto* Ce = dyn_cast<CXXConstructExpr>(Ein)) {
      translateCXXConstructExpr(Ce, Ctx, Alc);
    }
    else {
      // We don't understand the initializer
      ensureAddInstr( Builder.newUndefined() );
    }
  }

  return Alc;
}


til::SExpr *ClangTranslator::translateCXXDeleteExpr(const CXXDeleteExpr *De,
                                                    CallingContext *Ctx) {
  // TODO: need Ohmu free opcode.
  auto *E = Builder.newUndefined();
  ensureAddInstr(E);
  return E;
}


til::SExpr *
ClangTranslator::translateCXXConstructExpr(const CXXConstructExpr *Ce,
                                           CallingContext *Ctx,
                                           til::SExpr *Self) {
  til::SExpr* Fun = makeProjectFromDecl(nullptr, Ce->getConstructor());

  Fun = Builder.newApply(Fun, Self);
  for (const Expr* Arg : Ce->arguments()) {
    auto *A = translate(Arg, Ctx);
    Fun = Builder.newApply(Fun, A);
  }
  auto* C = Builder.newCall(Fun);
  return C;
}


til::SExpr *
ClangTranslator::translateDeclStmt(const DeclStmt *S, CallingContext *Ctx) {
  if (CapabilityExprMode)
    return nullptr;

  for (Decl* D : S->getDeclGroup()) {
    if (VarDecl *Vd = dyn_cast_or_null<VarDecl>(D)) {
      // Add local variables with trivial type to the variable map
      QualType Qt = Vd->getType();
      if (Qt.isTrivialType(Vd->getASTContext())) {
        auto *Einit = translate(Vd->getInit(), Ctx);
        auto *Typ = translateClangType(Qt);
        auto *Fld = Builder.newField(Typ, Einit);
        auto* Alc = Builder.newAlloc(Fld, til::Alloc::AK_Stack);
        Alc->setInstrName(Builder, Vd->getName());
        insertLocalVar(Vd, Alc);
      }
      else {
        auto *Typ = translateClangType(Qt);
        auto *Alc = Builder.newAlloc(Typ, til::Alloc::AK_Stack);
        Alc->setInstrName(Builder, Vd->getName());

        if (auto* Ein = Vd->getInit()) {
          if (auto* Ce = dyn_cast<CXXConstructExpr>(Ein)) {
            translateCXXConstructExpr(Ce, Ctx, Alc);
          }
          else {
            // We don't understand the initializer
            ensureAddInstr( Builder.newUndefined() );
          }
        }
        insertLocalVar(Vd, Alc);
      }
    }
    // TODO: don't just ignore these.
  }
  return nullptr;
}


til::SExpr*
ClangTranslator::translateCharacterLiteral(const CharacterLiteral *L,
                                           CallingContext *Ctx) {
  unsigned V = L->getValue();
  if (V < (1 << 8))
    return Builder.newLiteralT<uint8_t>( static_cast<uint8_t>(V) );
  if (V < (1 << 16))
    return Builder.newLiteralT<uint16_t>( static_cast<uint16_t>(V) );
  return Builder.newLiteralT<uint32_t>( static_cast<uint32_t>(V) );
}


til::SExpr*
ClangTranslator::translateCXXBoolLiteralExpr(const CXXBoolLiteralExpr *L,
                                             CallingContext *Ctx) {
  return Builder.newLiteralT<bool>(L->getValue());
}


til::SExpr* ClangTranslator::translateIntegerLiteral(const IntegerLiteral *L,
                                                     CallingContext *Ctx) {
  til::BaseType Bt = getBaseTypeFromClangType( L->getType() );
  llvm::APInt V = L->getValue();

  if (Bt.Base == til::BaseType::BT_Int) {
    switch (Bt.Size) {
      case til::BaseType::ST_8:
        return Builder.newLiteralT<int8_t>(
            static_cast<int8_t>(V.getSExtValue()) );
      case til::BaseType::ST_16:
        return Builder.newLiteralT<int16_t>(
            static_cast<int16_t>(V.getSExtValue()) );
      case til::BaseType::ST_32:
        return Builder.newLiteralT<int32_t>(
            static_cast<int32_t>(V.getSExtValue()) );
      case til::BaseType::ST_64:
        return Builder.newLiteralT<int64_t>(
            static_cast<int64_t>(V.getSExtValue()) );
      default:
        break;
    }
  }
  else if (Bt.Base == til::BaseType::BT_UnsignedInt) {
    switch (Bt.Size) {
      case til::BaseType::ST_8:
        return Builder.newLiteralT<uint8_t>(
            static_cast<uint8_t>(V.getZExtValue()) );
      case til::BaseType::ST_16:
        return Builder.newLiteralT<uint16_t>(
            static_cast<uint16_t>(V.getZExtValue()) );
      case til::BaseType::ST_32:
        return Builder.newLiteralT<uint32_t>(
            static_cast<uint32_t>(V.getZExtValue()) );
      case til::BaseType::ST_64:
        return Builder.newLiteralT<uint64_t>(
            static_cast<uint64_t>(V.getZExtValue()) );
      default:
        break;
    }
  }
  return Builder.newUndefined();
}


til::SExpr* ClangTranslator::translateFloatingLiteral(const FloatingLiteral *L,
                                                      CallingContext *Ctx) {
  til::BaseType Bt = getBaseTypeFromClangType( L->getType() );
  llvm::APFloat V = L->getValue();

  if (Bt.Size == til::BaseType::ST_32)
    return Builder.newLiteralT<float>( V.convertToFloat() );
  else if (Bt.Size == til::BaseType::ST_64)
    return Builder.newLiteralT<double>( V.convertToDouble() );
  return Builder.newUndefined();
}


til::SExpr*
ClangTranslator::translateObjCStringLiteral(const ObjCStringLiteral *L,
                                            CallingContext *Ctx) {
  // TODO: deal with different kinds of strings: ASCII, UTF8, etc.
  return Builder.newLiteralT<StringRef>(L->getString()->getString());
}


til::SExpr* ClangTranslator::translateStringLiteral(const StringLiteral *L,
                                                    CallingContext *Ctx) {
  // TODO: deal with different kinds of strings: ASCII, UTF8, etc.
  // We don't use StringLiteral::getString() since it assumes that the
  // character width is equal to 1, which is not true for C++ code in general.
  // The current code is still incorrect since it merges different kinds of
  // strings, but avoids assertion violations.
  return Builder.newLiteralT<StringRef>(L->getBytes());
}


til::SExpr*
ClangTranslator::translateCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *L,
                                                CallingContext *Ctx) {
  return Builder.newLiteralT<void*>(nullptr);
}


til::SExpr* ClangTranslator::translateGNUNullExpr(const GNUNullExpr *L,
                                                  CallingContext *Ctx) {
  return Builder.newLiteralT<void*>(nullptr);
}


void ClangTranslator::enterCFG(CFG *Cfg, const NamedDecl *D,
                               const CFGBlock *First) {
  // Get parameters and return type from clang Decl
  QualType RType;
  ArrayRef<ParmVarDecl*> Parms;

  if (auto* Fcd = dyn_cast<ObjCMethodDecl>(D)) {
    Parms  = Fcd->parameters();
    RType  = Fcd->getReturnType();
  }
  else {
    auto* Fd = dyn_cast<FunctionDecl>(D);
    Parms  = Fd->parameters();
    RType  = Fd->getReturnType();
  }

  std::vector<std::pair<til::SExpr*, til::Variable*>>  FunParams;

  // Create an enclosing top-level function.
  til::Function* TopFun = nullptr;
  til::Function* OldFun = nullptr;

  if (isa<CXXMethodDecl>(D)) {
    // Explicitly add "this" (SelfVar)
    Builder.enterScope(SelfVar->variableDecl());
    TopFun = Builder.newFunction(SelfVar->variableDecl(), nullptr);
    OldFun = TopFun;
    ++NumFunctionParams;
  }

  for (auto *Pm : Parms) {
    til::SExpr* Typ = translateClangType(Pm->getType());
    auto* Fvd = Builder.newVarDecl(til::VarDecl::VK_Fun, Pm->getName(), Typ);
    auto *Fun = Builder.newFunction(Fvd, nullptr);

    Builder.enterScope(Fvd);
    ++NumFunctionParams;   // We'll exit scope in exitCFG
    FunParams.push_back(std::make_pair(Typ, Builder.newVariable(Fvd)));

    if (!TopFun)
      TopFun = Fun;
    if (OldFun)
      OldFun->setBody(Fun);
    OldFun = Fun;
  }

  til::SExpr* Rty = translateClangType(RType);
  auto* Funbody = Builder.newCode(Rty, nullptr);
  if (OldFun)
    OldFun->setBody(Funbody);

  // Set the top level slot.
  // If there are no arguments, the slot just contains the function body.
  til::SExpr *Topdef;
  if (TopFun)
    Topdef = TopFun;
  else
    Topdef = Funbody;
  StringRef SltNm = getMangledValueName(D);
  TopLevelSlot = Builder.newSlot(SltNm, Topdef);

  // Create a new CFG
  unsigned NBlocks = Cfg->getNumBlockIDs();
  Builder.beginCFG(nullptr, NBlocks, 0);
  Funbody->setBody(Builder.currentCFG());

  // Create map from clang blocks to til::BasicBlocks
  BMap.resize(NBlocks, nullptr);
  for (auto *B : *Cfg) {
    if (B == &Cfg->getEntry()) {
      insertBlock(B, Builder.currentCFG()->entry());
    }
    else if (B == &Cfg->getExit()) {
      insertBlock(B, Builder.currentCFG()->exit());
    }
    else {
      auto *BB = Builder.newBlock();
      insertBlock(B, BB);
    }
  }

  // Add function parameters as allocations in entry block.
  Builder.beginBlock(Builder.currentCFG()->entry());

  unsigned i = 0;
  for (auto& Pm : FunParams) {
    til::Alloc* Alc;
    if (!Parms[i]->getType()->isReferenceType()) {
      // Ohmu parameters cannot be modified.
      // So for non-reference types, we must create a local variable that
      // is initialized to the parameter.
      auto *Fld = Builder.newField(Pm.first, Pm.second);
      Alc = Builder.newAlloc(Fld, til::Alloc::AK_Stack);
      Alc->setInstrName(Builder, Pm.second->varName());
      insertLocalVar(Parms[i], Alc);
    }
    else {
      insertLocalVar(Parms[i], Pm.second);
    }
    ++i;
  }
}


void ClangTranslator::enterCFGBlockBody(const CFGBlock *B) {
  if (Builder.currentBB())
    return;

  // Intialize TIL basic block and add it to the CFG.
  auto *BB = lookupBlock(B);
  Builder.beginBlock(BB);
}


void ClangTranslator::handleStatement(const Stmt *S) {
  translate(S, nullptr);
}


void ClangTranslator::handleDestructorCall(const VarDecl *Vd,
                                           const CXXDestructorDecl *Dd) {
  auto* V = lookupLocalVar(Vd);
  til::SExpr* Fun = makeProjectFromDecl(nullptr, Dd);

  Fun = Builder.newApply(Fun, V);
  Builder.newCall(Fun);
}


void ClangTranslator::handleDestructorCall(const Expr *E,
                                           const CXXDestructorDecl *Dd) {
  auto *Ep = translate(E, nullptr);
  til::SExpr* Fun = makeProjectFromDecl(nullptr, Dd);

  Fun = Builder.newApply(Fun, Ep);
  Builder.newCall(Fun);
}


void ClangTranslator::exitCFGBlockBody(const CFGBlock *B) {
  int N = B->succ_size();
  const Stmt* Term = B->getTerminator().getStmt();

  if (N == 0) {
    // End with null terminator
    Builder.endBlock(nullptr);
  }
  else if (N == 1) {
    auto It = B->succ_begin();
    til::BasicBlock *Bb = *It ? lookupBlock(*It) : nullptr;
    if (Bb == Builder.currentCFG()->exit() && !B->empty()) {
      auto Last = B->back().getAs<CFGStmt>();
      if (Last.hasValue()) {
        auto* Ret = dyn_cast_or_null<ReturnStmt>(Last->getStmt());
        auto* Rexp = Ret ? translate(Ret->getRetValue(), nullptr) : nullptr;
        Builder.newGoto(Bb, Rexp);
        return;
      }
    }
    if (Bb) {
      Builder.newGoto(Bb);
      return;
    }
    return;
  }
  else if (N == 2 && !isa<SwitchStmt>(Term)) {
    auto It = B->succ_begin();
    til::SExpr *C = translate(B->getTerminatorCondition(true), nullptr);
    CFGBlock* Cb1 = *It;
    ++It;
    CFGBlock* Cb2 = *It;

    til::BasicBlock *Bb1temp = Cb1 ? lookupBlock(Cb1) : nullptr;
    til::BasicBlock *Bb2temp = Cb2 ? lookupBlock(Cb2) : nullptr;

    // Insert dummy blocks to eliminate critical edges, if necessary.
    auto* Bbexit = Builder.currentCFG()->exit();
    auto* Bb1 = Bb1temp;
    if (Bb1 == Bbexit || (Cb1 && (Cb1->pred_size() > 1)))
      Bb1 = Builder.newBlock();

    auto* Bb2 = Bb2temp;
    if (Bb2 == Bbexit || (Cb2 && (Cb2->pred_size() > 1)))
      Bb2 = Builder.newBlock();

    // End the current block.
    Builder.newBranch(C, Bb1, Bb2);

    // Finish dummy blocks, if necessary.
    if (Bb1 != Bb1temp) {
      Builder.beginBlock(Bb1);
      Builder.newGoto(Bb1temp);
    }
    if (Bb2 != Bb2temp) {
      Builder.beginBlock(Bb2);
      Builder.newGoto(Bb2temp);
    }
    return;
  }
  else {
    const SwitchStmt* SwSt = dyn_cast<SwitchStmt>(Term);
    if (!SwSt) {
      // End with null terminator.  This should never happen.
      Builder.endBlock(nullptr);
    }

    til::SExpr *C = translate(SwSt->getCond(), nullptr);

    // Collect label expressions before creating the switch.
    std::vector<til::SExpr*> Labels(N, nullptr);
    auto It = B->succ_begin();
    for (int i = 0; i < N; ++i) {
      CFGBlock *Cb = *It;
      til::SExpr *Lab;

      auto *LabSt = Cb ? Cb->getLabel() : nullptr;
      if (auto *CaseSt = dyn_cast_or_null<CaseStmt>(LabSt)) {
        // TODO: handle RHS()
        Lab = translate(CaseSt->getLHS(), nullptr);
      } else if (dyn_cast_or_null<DefaultStmt>(LabSt)) {
        Lab = Builder.newWildcard();
      } else {
        Lab = Builder.newUndefined();
      }

      Labels[i] = Lab;
      ++It;
    }

    // Create the switch instruction
    auto *Sw = Builder.newSwitch(C, N);

    // Fill in the labels and blocks.
    auto *Bbexit = Builder.currentCFG()->exit();
    It = B->succ_begin();
    for (int i = 0; i < N; ++i) {
       CFGBlock *Cb = *It;
      til::BasicBlock *Bbtemp = Cb ? lookupBlock(Cb) : nullptr;

      // Insert dummy blocks to eliminate critical edges, if necessary.
      auto *Bb = Bbtemp;
      if (Bb == Bbexit || (Cb && (Cb->pred_size() > 1))) {
        Bb = Builder.newBlock();
        Builder.beginBlock(Bb);
        Builder.newGoto(Bbtemp);
      }

      Builder.addSwitchCase(Sw, Labels[i], Bb);
      ++It;
    }
  }
}


void ClangTranslator::exitCFG(const CFGBlock *Last) {
  til::SCFG* Scfg = Builder.currentCFG();
  Builder.endCFG();

  // Exit the scope of the clang Decl
  unsigned i = NumFunctionParams;
  while (i > 0) {
    Builder.exitScope();
    --i;
  }
  NumFunctionParams = 0;

  Scfg->renumber();

  // Uncomment for debugging:
  // std::cout << "\n--- C++ Translation ---\n";
  // til::TILDebugPrinter::print(TopLevelSlot, std::cout);
  // std::cout << "\n";

  Scfg->computeNormalForm();

  // Uncomment for debugging:
  // std::cout << "\n--- Normal Form ---\n";
  // til::TILDebugPrinter::print(TopLevelSlot, std::cout);
  // std::cout << "\n";

  if (SSAMode) {
    til::SSAPass ssaPass(Builder.arena());
    ssaPass.traverseAll(TopLevelSlot);

    // Uncomment for debugging;
    // std::cout << "\n--- After SSA ---\n";
    // til::TILDebugPrinter::print(TopLevelSlot, std::cout);
    // std::cout << "\n";
  }
}


void ClangTranslator::dumpTopLevelSlot() {
  til::TILDebugPrinter::print(TopLevelSlot, std::cout);
}


}  // end namespace tilcpp
}  // end namespace clang

