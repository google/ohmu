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

#include "base/LLVMDependencies.h"

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

  // TODO: don't hardcode the minimim size!
  static const SizeCode MinimumIntegerSize = ST_32;

  inline static SizeCode getSizeCode(unsigned nbytes);

  template <class T>
  inline static BaseType getBaseType();

  bool operator==(const BaseType& Vt) const {
    return Base == Vt.Base  &&  Size == Vt.Size  &&  VectSize == Vt.VectSize;
  }
  bool operator!=(const BaseType& Vt) const { return !(*this == Vt); }

  /// Return true if this is a simple (i.e. non-pointer) type
  bool isSimple() {
    return Base != BT_Pointer;
  }

  bool isPointer() {
    return Base == BT_Pointer;
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

  /// Promote to minimim integer size type.
  /// Returns true if promotion was necessary.
  bool promoteInteger() {
    // TODO: don't hardcode the minimim size!
    if (isIntegral() && Size < MinimumIntegerSize) {
      Size = MinimumIntegerSize;
      return true;
    }
    return false;
  }

  /// Encode as 8-bit integer, with a single bit indicating vector or not.
  uint8_t asUInt8() {
    uint8_t VectorBit = (VectSize <= 1) ? 0 : (1 << 7);
    return static_cast<uint8_t>(VectorBit | (Size << 4) | Base);
  }

  /// Encode as 16-bit integer
  uint16_t asUInt16() {
    return static_cast<uint16_t>((VectSize << 16) | (Size << 4) | Base);
  }

  // Set value from 8-bit integer, and return true if the vector bit set.
  bool fromUInt8(uint8_t V) {
    Base = static_cast<BaseCode>(V & 0xF);
    Size = static_cast<SizeCode>((V >> 4) & 0x7);
    VectSize = 0;
    return (V & 0x08) != 0;
  }

  // Set value from encoded 16-bit integer.
  void fromUInt16(uint16_t V) {
    Base     = static_cast<BaseCode>(V & 0xF);
    Size     = static_cast<SizeCode>((V >> 4) & 0xF);
    VectSize = static_cast<uint8_t> ((V >> 16) & 0xFF);
  }

  const char* getTypeName();

  BaseType() = default;
  BaseType(BaseCode B, SizeCode Sz, unsigned char Vs)
      : Base(B), Size(Sz), VectSize(Vs)
  { }

  BaseCode Base;
  SizeCode Size;
  uint8_t  VectSize;  // 0 for scalar, otherwise num elements in vector
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


/// Parse base type, and call F<Ty>, with Ty set to the static type.
template< template<typename> class F >
class BtBr {
public:
  typedef typename F<void>::ReturnType ReturnType;

  /// Parse base type, and call F<Ty>(Args), with Ty set to the static type.
  /// Return Default on failure.
  template<typename... ArgTypes >
  static ReturnType branch(BaseType Bt, ArgTypes... Args) {
    switch (Bt.Base) {
    case BaseType::BT_Void:
      break;
    case BaseType::BT_Bool:
      return F<bool>::action(Args...);
    case BaseType::BT_Int: {
      switch (Bt.Size) {
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
      switch (Bt.Size) {
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
    case BaseType::BT_Float: {
      switch (Bt.Size) {
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
    }
    return F<void>::defaultAction(Args...);
  }


  /// Parse base type, and call F<Ty>(Args), with Ty set to the static type.
  /// This version only parses numeric (int, and float) types,
  /// and it only handles integer types have been promoted to a minimum size.
  /// Returns Default on failure.
  template<typename... ArgTypes >
  static ReturnType branchOnNumeric(BaseType Bt, ArgTypes... Args) {
    switch (Bt.Base) {
    case BaseType::BT_Int: {
      switch (Bt.Size) {
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
      switch (Bt.Size) {
      case BaseType::ST_32:
        return F<uint32_t>::action(Args...);
      case BaseType::ST_64:
        return F<uint64_t>::action(Args...);
      default:
        break;
      }
      break;
    }
    case BaseType::BT_Float: {
      switch (Bt.Size) {
      case BaseType::ST_32:
        return F<float>::action(Args...);
      case BaseType::ST_64:
        return F<double>::action(Args...);
      default:
        break;
      }
      break;
    }
    default:
      break;
    }
    return F<void>::defaultAction(Args...);
  }


  /// Parse base type, and call F<Ty>(Args), with Ty set to the static type.
  /// This version only parses the integer (signed and unsigned types),
  /// and it only handles integer types have been promoted to a minimum size.
  /// Returns Default on failure.
  template<typename... ArgTypes >
  static ReturnType branchOnIntegral(BaseType Bt, ArgTypes... Args) {
    switch (Bt.Base) {
    case BaseType::BT_Int: {
      switch (Bt.Size) {
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
      switch (Bt.Size) {
      case BaseType::ST_32:
        return F<uint32_t>::action(Args...);
      case BaseType::ST_64:
        return F<uint64_t>::action(Args...);
      default:
        break;
      }
      break;
    }
    default:
      break;
    }
    return F<void>::defaultAction(Args...);
  }

};


}  // end namespace til
}  // end namespace ohmu


#endif  // OHMU_TIL_TILBASETYPE_H
