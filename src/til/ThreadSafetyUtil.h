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

#include "../base/MemRegion.h"
#include "../base/SimpleArray.h"
#include "../base/Util.h"
#include "../parser/Token.h"

// pull in all of the cast operations.
using namespace ohmu;


namespace clang {

typedef ohmu::parsing::SourceLocation SourceLocation;

// Define the basic clang data types that the TIL depends on.
class ValueDecl {
public:
  StringRef getName() const { return name_; }

private:
  StringRef name_;
};


class Stmt { };
class Expr : public Stmt { };
class CallExpr : public Expr { };


namespace threadSafety {
namespace til {

enum TIL_UnaryOpcode {
  UOP_None = 0,     //  no-op
  UOP_BitNot,       //  ~
  UOP_LogicNot      //  !
};


enum TIL_BinaryOpcode {
  BOP_Mul,          //  *
  BOP_Div,          //  /
  BOP_Add,          //  +
  BOP_Sub,          //  -
  BOP_Shl,          //  <<
  BOP_Shr,          //  >>
  BOP_BitAnd,       //  &
  BOP_BitXor,       //  ^
  BOP_BitOr,        //  |
  BOP_Eq,           //  ==
  BOP_Neq,          //  !=
  BOP_Lt,           //  <
  BOP_Leq,          //  <=
  BOP_LogicAnd,     //  &&
  BOP_LogicOr       //  ||
};


enum TIL_CastOpcode {
  CAST_None = 0,
  CAST_extendNum,   // extend precision of numeric type
  CAST_truncNum,    // truncate precision of numeric type
  CAST_toFloat,     // convert to floating point type
  CAST_toInt,       // convert to integer type
};


} // end namespace til
} // end namespace threadSafety
} // end namespace clang

#endif   // OHMU_THREAD_SAFETY_UTIL_H

