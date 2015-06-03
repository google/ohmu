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


/// Implements a post-order visitor.
template<class Self>
class Visitor : public Traversal<Self>,
                public DefaultScopeHandler,
                public DefaultReducer {
public:
  typedef Traversal<Self> SuperTv;

  Self* self() { return static_cast<Self*>(this); }

  Visitor() : Success(true) { }

  static bool visit(SExpr *E) {
    Self Visitor;
    Visitor.traverseAll(E);
    return Visitor.Success;
  }

  /// Visit routines may invoke fail() to abort the visitor.
  void fail() { Success = false; }

  /// Override traverse to abort traversal on failure.
  template <class T>
  void traverse(T* E, TraversalKind K) {
    if (Success)
      SuperTv::traverse(E, K);
  }

protected:
  bool Success;
};


} // end namespace til
} // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVISITOR_H
