//===- test_visitor.cpp ----------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//
//
// Compile-only test.
// Instantiates some visitor and traverser templates for testing purposes.
//
//===----------------------------------------------------------------------===//

#include "til/TILVisitor.h"

namespace ohmu {
namespace til  {


/// This is an alternative implementation of visitor, which just uses
/// DefaultReducer.  It is here as a test case for DefaultReducer.
template<class Self>
class AlternateVisitor : public Traversal<Self, VisitReducerMap>,
                         public DefaultReducer<Self, VisitReducerMap>,
                         public DefaultScopeHandler<VisitReducerMap> {
public:
  typedef Traversal<Self, VisitReducerMap> SuperTv;

  Self* self() { return static_cast<Self*>(this); }

  AlternateVisitor() : Success(true) { }

  bool visitSExpr(SExpr& Orig) {
    return true;
  }

  bool reduceSExpr(SExpr& Orig) {
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

private:
  bool Success;
};


class SimpleVisitor : public Visitor<SimpleVisitor> { };

class SimpleVisitor2 : public AlternateVisitor<SimpleVisitor2> { };


void test(SExpr* E) {
  SimpleVisitor::visit(E);
  SimpleVisitor2::visit(E);
}


}  // end namespace til
}  // end namespace ohmu
