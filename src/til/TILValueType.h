//===- TILValueType.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//
//
// Defines
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVALUETYPE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVALUETYPE_H

#include "TILDependencies.h"

#include <stdint.h>

namespace ohmu {
namespace til {


/// ValueTypes are data types that can actually be held in registers.
/// All variables and expressions must have a value type.
/// Pointer types are further subdivided into the various heap-allocated
/// types, such as functions, records, etc.
/// Structured types that are passed by value (e.g. complex numbers)
/// require special handling; they use BT_ValueRef, and size ST_0.
struct ValueType {
  enum BaseType : unsigned char {
    BT_Void = 0,
    BT_Bool,
    BT_Int,
    BT_Float,
    BT_String,    // String literals
    BT_Pointer,   // Base type for all pointers
    BT_ValueRef
  };

  enum SizeType : unsigned char {
    ST_0 = 0,
    ST_1,
    ST_8,
    ST_16,
    ST_32,
    ST_64,
    ST_128
  };

  inline static SizeType getSizeType(unsigned nbytes);

  template <class T>
  inline static ValueType getValueType();

  bool operator==(const ValueType& Vt) const {
    return Base   == Vt.Base   && Size     == Vt.Size &&
           Signed == Vt.Signed && VectSize == Vt.VectSize;
  }
  bool operator!=(const ValueType& Vt) const { return !(*this == Vt); }

  /// Return true if this is a numeric (int or float) type
  bool isNumeric() {
    return (Base == BT_Int || Base == BT_Float);
  }

  /// Encode as 32-bit integer
  unsigned asInt32() {
    return (VectSize << 24) | (Signed << 16) | (Size << 8) | Base;
  }

  const char* getTypeName();

  ValueType(BaseType B, SizeType Sz, bool S, unsigned char VS)
      : Base(B), Size(Sz), Signed(S), VectSize(VS)
  { }

  BaseType      Base;
  SizeType      Size;
  bool          Signed;
  unsigned char VectSize;  // 0 for scalar, otherwise num elements in vector
};


inline ValueType::SizeType ValueType::getSizeType(unsigned nbytes) {
  switch (nbytes) {
    case 1: return ST_8;
    case 2: return ST_16;
    case 4: return ST_32;
    case 8: return ST_64;
    case 16: return ST_128;
    default: return ST_0;
  }
}

template<>
inline ValueType ValueType::getValueType<void>() {
  return ValueType(BT_Void, ST_0, false, 0);
}

template<>
inline ValueType ValueType::getValueType<bool>() {
  return ValueType(BT_Bool, ST_1, false, 0);
}

template<>
inline ValueType ValueType::getValueType<int8_t>() {
  return ValueType(BT_Int, ST_8, true, 0);
}

template<>
inline ValueType ValueType::getValueType<uint8_t>() {
  return ValueType(BT_Int, ST_8, false, 0);
}

template<>
inline ValueType ValueType::getValueType<int16_t>() {
  return ValueType(BT_Int, ST_16, true, 0);
}

template<>
inline ValueType ValueType::getValueType<uint16_t>() {
  return ValueType(BT_Int, ST_16, false, 0);
}

template<>
inline ValueType ValueType::getValueType<int32_t>() {
  return ValueType(BT_Int, ST_32, true, 0);
}

template<>
inline ValueType ValueType::getValueType<uint32_t>() {
  return ValueType(BT_Int, ST_32, false, 0);
}

template<>
inline ValueType ValueType::getValueType<int64_t>() {
  return ValueType(BT_Int, ST_64, true, 0);
}

template<>
inline ValueType ValueType::getValueType<uint64_t>() {
  return ValueType(BT_Int, ST_64, false, 0);
}

template<>
inline ValueType ValueType::getValueType<float>() {
  return ValueType(BT_Float, ST_32, true, 0);
}

template<>
inline ValueType ValueType::getValueType<double>() {
  return ValueType(BT_Float, ST_64, true, 0);
}

template<>
inline ValueType ValueType::getValueType<long double>() {
  return ValueType(BT_Float, ST_128, true, 0);
}

template<>
inline ValueType ValueType::getValueType<StringRef>() {
  return ValueType(BT_String, getSizeType(sizeof(StringRef)), false, 0);
}

template<>
inline ValueType ValueType::getValueType<void*>() {
  return ValueType(BT_Pointer, getSizeType(sizeof(void*)), false, 0);
}

}  // end namespace til
}  // end namespace ohmu


#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILVALUETYPE_H
