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

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

namespace clang {
namespace threadSafety {
namespace til {


class SimpleVisitReducer : public VisitReducer<SimpleVisitReducer> { };

class SimpleVisitor
  : public VisitTraversal<SimpleVisitor, SimpleVisitReducer> { };


class SimpleCopyReducer : public CopyReducerBase {
public:
  class ContextT : public CopyContext<ContextT, SimpleCopyReducer> {
  public:
    ContextT(SimpleCopyReducer *R) : CopyContext(R) { }
  };
};

class SimpleCopier
  : public CopyTraversal<SimpleCopier, SimpleCopyReducer> { };


void test(SExpr* E, MemRegionRef A) {
  SimpleVisitor::visit(E);
  SimpleCopier::rewrite(E, A);
}

}  // end namespace til
}  // end namespace threadSafety
}  // end namespace clang
