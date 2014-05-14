//===- ThreadSafetyUtil.cpp ------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//


#include "ThreadSafetyUtil.h"

namespace clang {

template <class StreamType>
void printSourceLiteral(clang::Expr *E, StreamType &SS) {
    const clang::Expr *CE = E->clangExpr();
    switch (CE->getStmtClass()) {
      case Stmt::IntegerLiteralClass:
        SS << cast<IntegerLiteral>(CE)->getValue().toString(10, true);
        return;
      case Stmt::StringLiteralClass:
        SS << "\"" << cast<StringLiteral>(CE)->getString() << "\"";
        return;
      case Stmt::CharacterLiteralClass:
      case Stmt::CXXNullPtrLiteralExprClass:
      case Stmt::GNUNullExprClass:
      case Stmt::CXXBoolLiteralExprClass:
      case Stmt::FloatingLiteralClass:
      case Stmt::ImaginaryLiteralClass:
      case Stmt::ObjCStringLiteralClass:
      default:
        SS << "#lit";
        return;
    }
}
