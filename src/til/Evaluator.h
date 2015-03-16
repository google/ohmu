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


template< template<typename> class F, class RTy, typename... ArgTypes >
RTy branchOnNumericType(BaseType BT, ArgTypes... Args) {
  switch (BT.Base) {
  case BaseType::BT_Void:
    break;
  case BaseType::BT_Bool:
    return F<bool>::action(Args...);
  case BaseType::BT_Int: {
    switch (BT.Size) {
    case BaseType::ST_8:
      return F<int8_t>::action(Args...);
    case BaseType::ST_16:
      return F<int16_t>::action(Args...);
    case BaseType::ST_32:
      return F<int32_t>::action(Args...);
    case BaseType::ST_64:
      return F<int64_t>::action(Args...);
    default:
      break;
    }
    break;
  }
  case BaseType::BT_UnsignedInt: {
    switch (BT.Size) {
    case BaseType::ST_8:
      return F<uint8_t>::action(Args...);
    case BaseType::ST_16:
      return F<uint16_t>::action(Args...);
    case BaseType::ST_32:
      return F<uint32_t>::action(Args...);
    case BaseType::ST_64:
      return F<uint64_t>::action(Args...);
    default:
      break;
    }
    break;
  }
  /*
  case BaseType::BT_Float: {
    switch (BT.Size) {
    case BaseType::ST_32:
      return F<float>::action(Args...);
    case BaseType::ST_64:
      return F<double>::action(Args...);
    default:
      break;
    }
    break;
  }
  case BaseType::BT_String:
    return F<StringRef>::action(Args...);
  case BaseType::BT_Pointer:
    return F<void*>::action(Args...);
  */
  default:
    return nullptr;
  }
  return nullptr;
}


#define DEFINE_BINARY_OP_CLASS_T(CName, OP, RTyp)                             \
template<class Ty>                                                            \
struct CName {                                                                \
  static LiteralT<RTyp>* action(MemRegionRef A, Literal* E0, Literal* E1) {   \
    return new (A) LiteralT<RTyp>(E0->as<Ty>()->value()  OP                   \
                                  E1->as<Ty>()->value());                     \
  }                                                                           \
};

#define DEFINE_BINARY_OP_CLASS(CName, OP) \
  DEFINE_BINARY_OP_CLASS_T(CName, OP, Ty)


namespace opclass {

DEFINE_BINARY_OP_CLASS(Add, +)
DEFINE_BINARY_OP_CLASS(Sub, -)
DEFINE_BINARY_OP_CLASS(Mul, *)
DEFINE_BINARY_OP_CLASS(Div, /)
DEFINE_BINARY_OP_CLASS(Rem, %)
DEFINE_BINARY_OP_CLASS(Shl, <<)
DEFINE_BINARY_OP_CLASS(Shr, <<)
DEFINE_BINARY_OP_CLASS(BitAnd, &)
DEFINE_BINARY_OP_CLASS(BitXor, ^)
DEFINE_BINARY_OP_CLASS(BitOr, |)

DEFINE_BINARY_OP_CLASS_T(Eq,  ==, bool)
DEFINE_BINARY_OP_CLASS_T(Neq, !=, bool)
DEFINE_BINARY_OP_CLASS_T(Lt,  <,  bool)
DEFINE_BINARY_OP_CLASS_T(Leq, <=, bool)

DEFINE_BINARY_OP_CLASS(LogicAnd, &&)
DEFINE_BINARY_OP_CLASS(LogicOr,  ||)

}  // end namespace opclass

#undef DEFINE_BINARY_OP_CLASS_T
#undef DEFINE_BINARY_OP_CLASS


#define ARGS Literal*, MemRegionRef, Literal*, Literal*

Literal* evaluateBinaryOp(TIL_BinaryOpcode Op, BaseType BT, MemRegionRef A,
                          Literal* E0, Literal* E1) {
  switch (Op) {
    case BOP_Add:
      return branchOnNumericType<opclass::Add, ARGS>(BT, A, E0, E1);
    case BOP_Sub:
      return branchOnNumericType<opclass::Sub, ARGS>(BT, A, E0, E1);
    case BOP_Mul:
      return branchOnNumericType<opclass::Mul, ARGS>(BT, A, E0, E1);
    case BOP_Div:
      return branchOnNumericType<opclass::Div, ARGS>(BT, A, E0, E1);
    case BOP_Rem:
      return branchOnNumericType<opclass::Rem, ARGS>(BT, A, E0, E1);
    case BOP_Shl:
      return branchOnNumericType<opclass::Shl, ARGS>(BT, A, E0, E1);
    case BOP_Shr:
      return branchOnNumericType<opclass::Shr, ARGS>(BT, A, E0, E1);
    case BOP_BitAnd:
      return branchOnNumericType<opclass::BitAnd, ARGS>(BT, A, E0, E1);
    case BOP_BitXor:
      return branchOnNumericType<opclass::BitXor, ARGS>(BT, A, E0, E1);
    case BOP_BitOr:
      return branchOnNumericType<opclass::BitOr,  ARGS>(BT, A, E0, E1);
    case BOP_Eq:
      return branchOnNumericType<opclass::Eq,  ARGS>(BT, A, E0, E1);
    case BOP_Neq:
      return branchOnNumericType<opclass::Neq, ARGS>(BT, A, E0, E1);
    case BOP_Lt:
      return branchOnNumericType<opclass::Lt,  ARGS>(BT, A, E0, E1);
    case BOP_Leq:
      return branchOnNumericType<opclass::Leq, ARGS>(BT, A, E0, E1);
    case BOP_Gt:
      return branchOnNumericType<opclass::Lt,  ARGS>(BT, A, E1, E0);
    case BOP_Geq:
      return branchOnNumericType<opclass::Leq, ARGS>(BT, A, E1, E0);
    case BOP_LogicAnd:
      return branchOnNumericType<opclass::LogicAnd, ARGS>(BT, A, E0, E1);
    case BOP_LogicOr:
      return branchOnNumericType<opclass::LogicOr,  ARGS>(BT, A, E0, E1);
  }
  return nullptr;
}

#undef ARGS


}  // endif namespace til
}  // endif namespace ohmu

#endif  // OHMU_TIL_EVALUATOR_H
