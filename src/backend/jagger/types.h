//===- types.h -------------------------------------------------*- C++ --*-===//
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

#pragma once

#include "util.h"

namespace Jagger {
namespace Wax {

//==============================================================================
// Type: Holds information about the type an object.
//==============================================================================

struct Type {
  enum : uchar { SIZE = 0x03, KIND = 0x1c, COUNT = 0x60, VARIANCE = 0x80 };
  enum Size : uchar { BYTE = 0x00, SHORT = 0x01, WORD = 0x02, LONG = 0x03 };
  enum Kind : uchar {
    BINARY_DATA = 0x00,
    UNSIGNED_INTEGER = 0x04,
    SIGNED_INTEGER = 0x08,
    FLOAT = 0x0c,
    VOID = 0x10,
    BOOLEAN = 0x14,
    ADDRESS = 0x18,
    STACK = 0x1c,
  };
  enum Count : uchar { SCALAR = 0x00, VEC2 = 0x20, VEC4 = 0x40 };
  enum Variance : uchar { VARYING = 0x00, UNIFORM = 0x80 };

  Type(Kind kind, Size size, Count count = SCALAR, Variance variance = VARYING)
    : data(kind | size | count | variance) {}
  static Type Void() { return Type(VOID); }

  Kind kind() const { return (Kind)(data & KIND); }
  Size size() const { return (Size)(data & SIZE); }
  Count count() const { return (Count)(data & COUNT); }
  Variance variance() const { return (Variance)(data & VARIANCE); }

  bool operator==(Type a) const { return data == a.data; }
  bool operator!=(Type a) const { return data != a.data; }

 private:
  explicit Type(uchar data) : data(data) {}
  uchar data;
};

//==============================================================================
// Code: The type codes.
//==============================================================================

enum Code : uchar {
  INVALID,
  CASE_HEADER,
  JOIN_HEADER,
  BYTES,
  ALIGNED_BYTES,
  ZERO,
  UNDEFINED_VALUE,
  STATIC_ADDRESS,
  USE,
  PHI,
  PHI_ARGUMENT,
  CALL,
  CALL_SPMD,
  RETURN,
  JUMP,
  BRANCH,
  SWITCH,

  COMPUTE_ADDRESS,
  PREFETCH,
  LOAD,
  STORE,
  MEM_SET,
  MEM_COPY,

  EXTRACT,
  INSERT,
  BROADCAST,
  PERMUTE,
  SHUFFLE,

  BIT_TEST,
  NOT,
  LOGIC,
  LOGIC3,
  SHIFT,
  BITFIELD_EXTRACT,
  BITFIELD_INSERT,
  BITFIELD_CLEAR,
  COUNT_ZEROS,
  POPCNT,

  COMPARE,
  MIN,
  MAX,
  NEG,
  ABS,
  ADD,
  SUB,
  MUL,
  DIV,

  MULHI,
  MOD,

  RCP,
  SQRT,
  RSQRT,
  EXP2,
  ROUND,
  CONVERT,
  FIXUP,

  ATOMIC_XCHG,
  ATOMIC_COMPARE_XCHG,
  ATOMIC_LOGIC_XCHG,
  ATOMIC_ADD_XCHG,

  NUM_OPCODES,
};

//==============================================================================
// Structural opcodes.
//==============================================================================

namespace Local {
struct Address : TypedStruct<Address, 1> {
  bool isStatic() const { return p.type(i) == STATIC_ADDRESS; }
};
}  // namespace Local

struct Invalid : TypedStruct<uint, 1> {};
struct CaseHeader : TypedStruct<uint, 1> {};
struct JoinHeader : TypedStruct<uint, 1> {};
struct AlignedBytes : TypedStruct<uint, 1> {};
struct Bytes : TypedStruct<uint, 1>{};
struct Zero : TypedStruct<uint, 1> {};
struct UndefinedValue : TypedStruct<uint, 1>{};
struct StaticAddress : TypedStruct<StaticAddress, 1> {};
struct Use : TypedStruct<uint, 1> {};
struct Phi : TypedStruct<uint, 1> {};
struct PhiArgument : TypedStruct<uint, 2> {
  Use arg() const { return field<Use>(i + 1); }
  Phi phi() const { return pointee().as<Phi>(); }
};
struct Call : TypedStruct<uint, 2> {
  uint& numArgs() const { return **this; }
  Local::Address callee() const { return field<Local::Address>(i + 1); }
  Use arg(size_t j) const { return field<Use>(i + 2 + j); }
};
struct CallSPMD : TypedStruct<uint, 3> {
  uint& numArgs() const { return **this; }
  Local::Address callee() const { return field<Local::Address>(i + 1); }
  uint& workCount() const { return *field<Bytes>(i + 2); }
  Use arg(size_t j) const { return field<Use>(i + 3 + j); }
};
struct Return : TypedStruct<uint, 1> {};
struct Jump : TypedStruct<uint, 2> {
  Local::Address target() const { return field<Local::Address>(i + 1); }
};
struct Branch : TypedStruct<uint, 4> {
  Use arg() const { return field<Use>(i + 1); }
  StaticAddress target0() const { return field<StaticAddress>(i + 2); }
  StaticAddress target1() const { return field<StaticAddress>(i + 3); }
};
struct Switch : TypedStruct<uint, 2> {
  StaticAddress target(size_t j) const {
    return field<StaticAddress>(i + 2 + j);
  }
  uint& numTargets() const { return **this; }
};

//==============================================================================
// Helper types.
//==============================================================================

namespace Local {
template <typename Payload>
struct Unary : TypedStruct<Payload, 2> {
  Use arg() const { return field<Use>(i + 1); }
};
template <typename Payload>
struct Binary : TypedStruct<Payload, 3> {
  Use arg0() const { return field<Use>(i + 1); }
  Use arg1() const { return field<Use>(i + 2); }
};
}  // namespace Local

//==============================================================================
// Memory opcodes.
//==============================================================================

struct ComputeAddressPayload {
  uchar scale;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct PrefetchPayload {
  enum Kind : uint { NT, L1, L2, L3 } kind;
};
struct LoadStorePayload {
  enum Flags : uchar { NON_TEMPORAL = 0x01, UNALIGNED = 0x02 } flags;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct MemOpPayload {
  uchar logAlignment;
  enum Flags : uchar { NON_TEMPORAL = 1 } flags;
};
struct ComputeAddress : TypedStruct<ComputeAddressPayload, 4> {
  Bytes disp() const { return field<Bytes>(i + 1); }
  Use base() const { return field<Use>(i + 2); }
  Use index() const { return field<Use>(i + 3); }
};
struct Prefetch : TypedStruct<PrefetchPayload, 2> {
  Local::Address address() const { return field<Local::Address>(i + 1); }
};
struct Load : TypedStruct<LoadStorePayload, 2> {
  Local::Address address() const { return field<Local::Address>(i + 1); }
};
struct Store : TypedStruct<LoadStorePayload, 3> {
  Local::Address address() const { return field<Local::Address>(i + 1); }
  Use arg() const { return field<Use>(i + 2); }
};
struct MemSet : TypedStruct<MemOpPayload, 4> {
  Local::Address address() const { return field<Local::Address>(i + 1); }
  Use value() const { return field<Use>(i + 2); }
  Use size() const { return field<Use>(i + 3); }
};
struct MemCopy : TypedStruct<MemOpPayload, 4> {
  Local::Address dst() const { return field<Local::Address>(i + 1); }
  Local::Address src() const { return field<Local::Address>(i + 2); }
  Use size() const { return field<Use>(i + 3); }
};

//==============================================================================
// Explicitly SIMD opcodes.
//==============================================================================

struct TypedPayload {
  uchar : 8;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ExtractInsertPayload {
  uchar lane;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ShufflePayload {
  uchar lane0 : 4;
  uchar lane1 : 4;
  uchar lane2 : 4;
  uchar lane3 : 4;
  uchar : 8;
  Type type;
};
struct Extract : Local::Unary<ExtractInsertPayload> {};
struct Insert : TypedStruct<ExtractInsertPayload, 3> {
  Use scalarArg() const { return field<Use>(i + 1); }
  Use vectorArg() const { return field<Use>(i + 2); }
};
struct BroadCast : Local::Unary<TypedPayload> {};
struct Permute : Local::Unary<ShufflePayload> {};
struct Shuffle : Local::Binary<ShufflePayload> {};

//==============================================================================
// Bit opcodes.
//==============================================================================

struct BitTestPayload {
  enum Kind : uchar { READ, CLEAR, SET, TOGGLE } kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct LogicPayload {
  enum Kind : uchar {
    FALSE, NOR, GT, NOTB, LT, NOTA, XOR, NAND,
    AND, EQ, A, GE, B, LE, OR, TRUE,
  } kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Logic3Payload {
  uchar kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ShiftPayload {
  enum Flags : uchar {
    SHIFT = 0x00,
    RIGHT = 0x00,
    LEFT = 0x01,
    ROTATE = 0x02,
    ARITHMETIC = 0x04
  } flags;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct BitFieldPayload {
  uchar begin;
  uchar end;
  uchar : 8;
  Type type;
};
struct CountZerosPayload {
  enum Kind : uchar { TRAILING, LEADING };
  uchar : 8;
  uchar : 8;
  Type type;
};
struct BitTest : Local::Unary<BitTestPayload> {};
struct Not : Local::Unary<TypedPayload> {};
struct Logic : Local::Binary<LogicPayload> {};
struct Logic3 : TypedStruct<Logic3Payload, 4>{
  Use arg0() const { return field<Use>(i + 1); }
  Use arg1() const { return field<Use>(i + 2); }
  Use arg2() const { return field<Use>(i + 3); }
};
struct Shift : Local::Binary<ShiftPayload> {};
struct BitfieldExtract : Local::Unary<BitFieldPayload> {};
struct BitfieldInsert : TypedStruct<BitFieldPayload, 3> {
  Use target() const { return field<Use>(i + 1); }
  Use source() const { return field<Use>(i + 2); }
};
struct BitfieldClear : Local::Unary<BitFieldPayload> {};
struct CountZeros : Local::Unary<CountZerosPayload> {};
struct PopCnt : Local::Unary<TypedPayload>{};

//==============================================================================
// Math opcodes.
//==============================================================================

struct ComparePayload {
  enum Kind : uchar {
    FALSE, LT, EQ, LE, GT, NEQ, GE, ORD,
    UNORD, LTU, EQU, LEU, GTU, NEQU, GEU, TRUE,
  } kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Compare : Local::Binary<ComparePayload> {};
struct Min : Local::Binary<TypedPayload> {};
struct Max : Local::Binary<TypedPayload> {};
struct Neg : Local::Unary<TypedPayload> {};
struct Abs : Local::Unary<TypedPayload> {};
struct Add : Local::Binary<TypedPayload> {};
struct Sub : Local::Binary<TypedPayload> {};
struct Mul : Local::Binary<TypedPayload> {};
struct Div : Local::Binary<TypedPayload> {};

//==============================================================================
// Integer math opcodes.
//==============================================================================

struct Mulhi : Local::Binary<TypedPayload> {};
struct Mod : Local::Binary<TypedPayload> {};

//==============================================================================
// Floating point math operations.
//==============================================================================

struct RoundPayload {
  enum Mode { EVEN, UP, DOWN, TRUNC, CURRENT } mode;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Rcp : Local::Unary<TypedPayload> {};
struct Sqrt : Local::Unary<TypedPayload> {};
struct Rsqrt : Local::Unary<TypedPayload> {};
struct Exp2 : Local::Unary<TypedPayload> {};
struct Round : Local::Unary<RoundPayload> {};
struct Convert : Local::Unary<TypedPayload> {};
struct Fixup : TypedStruct<TypedPayload, 3> {
  Bytes control() const { return field<Bytes>(i + 1); }
  Use arg() const { return field<Use>(i + 2); }
};

//==============================================================================
// Atomic operations.
//==============================================================================

struct AtomicXchg : Store {};
struct AtomicCompareXchg : TypedStruct<TypedPayload, 4> {
  Local::Address address() const { return field<Local::Address>(i + 1); }
  Use value() const { return field<Use>(i + 2); }
  Use comparand() const { return field<Use>(i + 3); }
};
struct AtomicLogicXchg : Store {};
struct AtomicAddXchg : Store {};
struct AtomicSubXchg : Store {};

}  // namespace Wax
}  // namespace Jagger
