//===- ThreadSafetyUtil.h --------------------------------------*- C++ --*-===//
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
// The Ohmu Typed Intermediate Langauge (or TIL) is also used for clang thread
// safety analysis.  This file provides the glue code that connects the TIL
// to either the clang and llvm standard libraries, or the ohmu libraries.
//
// This version connects it to the ohmu libraries.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_THREAD_SAFETY_UTIL_H
#define OHMU_THREAD_SAFETY_UTIL_H

#include "base/MemRegion.h"
#include "base/SimpleArray.h"
#include "base/Util.h"
#include "parser/Token.h"


// pull in all of the cast operations.
using namespace ohmu;


namespace clang {

typedef ohmu::parsing::SourceLocation SourceLocation;

// Define the basic clang data types that the TIL depends on.
class ValueDecl {
public:
  StringRef getName() const { return name_; }
  std::string getNameAsString() const { return name_.str(); }

private:
  StringRef name_;
};


class Stmt { };
class Expr : public Stmt { };
class CallExpr : public Expr { };

namespace threadSafety {
namespace til {

inline std::string getSourceLiteralString(const clang::Expr *E) {
  return "#lit";
}

} // end namespace til
} // end namespace threadSafety
} // end namespace clang

#endif   // OHMU_THREAD_SAFETY_UTIL_H

