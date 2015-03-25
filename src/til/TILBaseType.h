//===- TILBaseType.h --------------------------------------*- C++ --*-===//
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

#ifndef OHMU_TIL_TILBASETYPE_H
#define OHMU_TIL_TILBASETYPE_H

#include <stdint.h>


namespace ohmu {
namespace til {


/// BaseTypes are data types that can actually be held in registers.
/// All variables and expressions must have a base type.
/// Pointer types are further subdivided into the various heap-allocated
/// types, such as functions, records, etc.
struct BaseType {
  enum BaseCode : unsigned char {
    BT_Void = 0,
    BT_Bool,
    BT_Int,
    BT_UnsignedInt,
    BT_Float,
    BT_String,    // String literals
    BT_Pointer    // Base type for all pointers
  };

  enum SizeCode : unsigned char {
    ST_0 = 0,
    ST_1,
    ST_8,
    ST_16,
    ST_32,
    ST_64,
    ST_128
  };

  inline static SizeCode getSizeCode(unsigned nbytes);

  template <class T>
  inline static BaseType getBaseType();

  bool operator==(const BaseType& Vt) const {
    return Base == Vt.Base  &&  Size == Vt.Size  &&  VectSize == Vt.VectSize;
  }
  bool operator!=(const BaseType& Vt) const { return !(*this == Vt); }

  /// Return true if this is a simple (i.e. non-pointer) type
  bool isSimple() {
    return (Base != BT_Pointer);
  }

  /// Return true if this is a numeric (int or float) type
  bool isNumeric() {
    return (Base == BT_Int || Base == BT_Float);
  }

  /// Return true if this is either a signed or unsigned integer.
  bool isIntegral() {
    return (Base == BT_Int || Base == BT_UnsignedInt);
  }

  /// Return true if this is a signed integer or float.
  bool isSigned() {
    return (Base == BT_Int || Base == BT_Float);
  }

  /// Encode as 32-bit integer
  unsigned asInt32() {
    return (VectSize << 16) | (Size << 8) | Base;
  }

  const char* getTypeName();

  BaseType(BaseCode B, SizeCode Sz, unsigned char Vs)
      : Base(B), Size(Sz), VectSize(Vs)
  { }

  BaseCode      Base;
  SizeCode      Size;
  unsigned char VectSize;  // 0 for scalar, otherwise num elements in vector
};


inline BaseType::SizeCode BaseType::getSizeCode(unsigned nbytes) {
  switch (nbytes) {
    case 1:  return ST_8;
    case 2:  return ST_16;
    case 4:  return ST_32;
    case 8:  return ST_64;
    case 16: return ST_128;
    default: return ST_0;
  }
}

template<>
inline BaseType BaseType::getBaseType<void>() {
  return BaseType(BT_Void, ST_0, 0);
}

template<>
inline BaseType BaseType::getBaseType<bool>() {
  return BaseType(BT_Bool, ST_1, 0);
}

template<>
inline BaseType BaseType::getBaseType<int8_t>() {
  return BaseType(BT_Int, ST_8, 0);
}

template<>
inline BaseType BaseType::getBaseType<uint8_t>() {
  return BaseType(BT_UnsignedInt, ST_8, 0);
}

template<>
inline BaseType BaseType::getBaseType<int16_t>() {
  return BaseType(BT_Int, ST_16, 0);
}

template<>
inline BaseType BaseType::getBaseType<uint16_t>() {
  return BaseType(BT_UnsignedInt, ST_16, 0);
}

template<>
inline BaseType BaseType::getBaseType<int32_t>() {
  return BaseType(BT_Int, ST_32, 0);
}

template<>
inline BaseType BaseType::getBaseType<uint32_t>() {
  return BaseType(BT_UnsignedInt, ST_32, 0);
}

template<>
inline BaseType BaseType::getBaseType<int64_t>() {
  return BaseType(BT_Int, ST_64, 0);
}

template<>
inline BaseType BaseType::getBaseType<uint64_t>() {
  return BaseType(BT_UnsignedInt, ST_64, 0);
}

template<>
inline BaseType BaseType::getBaseType<float>() {
  return BaseType(BT_Float, ST_32, 0);
}

template<>
inline BaseType BaseType::getBaseType<double>() {
  return BaseType(BT_Float, ST_64, 0);
}

template<>
inline BaseType BaseType::getBaseType<StringRef>() {
  return BaseType(BT_String, getSizeCode(sizeof(StringRef)), 0);
}

template<>
inline BaseType BaseType::getBaseType<void*>() {
  return BaseType(BT_Pointer, getSizeCode(sizeof(void*)), 0);
}

}  // end namespace til
}  // end namespace ohmu


#endif  // OHMU_TIL_TILBASETYPE_H
