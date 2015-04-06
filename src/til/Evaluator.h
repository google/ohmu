//===- TypedEvaluator.h ----------------------------------------*- C++ --*-===//
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

#ifndef OHMU_TIL_EVALUATOR_H
#define OHMU_TIL_EVALUATOR_H

#include "TIL.h"

namespace ohmu {
namespace til {

// TODO: minimum integer size should not be hardcoded at ST_32.
// See TILBaseType.h


#define DEFINE_BINARY_OP_CLASS(CName, OP, RTy)                                \
template<class Ty1>                                                           \
struct CName {                                                                \
  typedef Literal* ReturnType;                                                \
  static Literal* defaultAction(MemRegionRef A, Literal*, Literal*) {         \
    return nullptr;                                                           \
  }                                                                           \
  static LiteralT<RTy>* action(MemRegionRef A, Literal* E0, Literal* E1) {    \
    return new (A) LiteralT<RTy>(E0->as<Ty1>()->value()  OP                   \
                                 E1->as<Ty1>()->value());                     \
  }                                                                           \
};


namespace opclass {

DEFINE_BINARY_OP_CLASS(Add,      +,   Ty1)
DEFINE_BINARY_OP_CLASS(Sub,      -,   Ty1)
DEFINE_BINARY_OP_CLASS(Mul,      *,   Ty1)
DEFINE_BINARY_OP_CLASS(Div,      /,   Ty1)
DEFINE_BINARY_OP_CLASS(Rem,      %,   Ty1)
DEFINE_BINARY_OP_CLASS(Shl,      <<,  Ty1)
DEFINE_BINARY_OP_CLASS(Shr,      >>,  Ty1)
DEFINE_BINARY_OP_CLASS(BitAnd,   &,   Ty1)
DEFINE_BINARY_OP_CLASS(BitXor,   ^,   Ty1)
DEFINE_BINARY_OP_CLASS(BitOr,    |,   Ty1)

DEFINE_BINARY_OP_CLASS(Eq,       ==,  bool)
DEFINE_BINARY_OP_CLASS(Neq,      !=,  bool)
DEFINE_BINARY_OP_CLASS(Lt,       <,   bool)
DEFINE_BINARY_OP_CLASS(Leq,      <=,  bool)

DEFINE_BINARY_OP_CLASS(LogicAnd, &&,  Ty1)
DEFINE_BINARY_OP_CLASS(LogicOr,  ||,  Ty1)

}  // end namespace opclass

#undef DEFINE_BINARY_OP_CLASS


#define ARGS(OP) opclass::OP, Literal*, MemRegionRef, Literal*, Literal*

Literal* evaluateBinaryOp(TIL_BinaryOpcode Op, BaseType Bt, MemRegionRef A,
                          Literal* E0, Literal* E1) {
  switch (Op) {
    case BOP_Add:
      return BtBr<opclass::Add>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Sub:
      return BtBr<opclass::Sub>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Mul:
      return BtBr<opclass::Mul>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Div:
      return BtBr<opclass::Div>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Rem:
      return BtBr<opclass::Rem>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_Shl:
      return BtBr<opclass::Shl>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_Shr:
      return BtBr<opclass::Shr>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_BitAnd:
      return BtBr<opclass::BitAnd>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_BitXor:
      return BtBr<opclass::BitXor>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_BitOr:
      return BtBr<opclass::BitOr>::branchOnIntegral(Bt, A, E0, E1);
    case BOP_Eq:
      return BtBr<opclass::Eq>::branch(Bt, A, E0, E1);
    case BOP_Neq:
      return BtBr<opclass::Neq>::branch(Bt, A, E0, E1);
    case BOP_Lt:
      return BtBr<opclass::Lt>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Leq:
      return BtBr<opclass::Leq>::branchOnNumeric(Bt, A, E0, E1);
    case BOP_Gt:
      return BtBr<opclass::Lt>::branchOnNumeric(Bt, A, E1, E0);
    case BOP_Geq:
      return BtBr<opclass::Leq>::branchOnNumeric(Bt, A, E1, E0);
    case BOP_LogicAnd:
      return opclass::LogicAnd<bool>::action(A, E0, E1);
    case BOP_LogicOr:
      return opclass::LogicOr<bool>::action(A, E0, E1);
  }
  return nullptr;
}

#undef ARGS


}  // endif namespace til
}  // endif namespace ohmu

#endif  // OHMU_TIL_EVALUATOR_H
