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

namespace jagger {
namespace wax {
struct Block {
  bool dominates(const Block& other) const {
    return other.domTreeID - domTreeID < domTreeSize;
  }
  bool postDominates(const Block& other) const {
    return other.postDomTreeID - postDomTreeID < postDomTreeSize;
  }
  uint dominator;
  uint domTreeID;
  uint domTreeSize;
  uint postDominator;
  uint postDomTreeID;
  uint postDomTreeSize;
  uint caseIndex;
  uint phiIndex;
  uint loopDepth;
  uint blockID;
  Range events;
  Range successors;
  Range predecessors;
};

struct Function {
  Range blocks;
  // calling convension.
  uint stackSpace;
};

struct StaticData {
  Range bytes;
  uint alignment;
};

struct Module {
  Module() {}
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;
  Array<Block> blockArray;
  Array<Function> functionArray;
  Array<uint> neighborArray;
  TypedArray instrArray;
  Array<StaticData> zeroDataEntries;
  Array<StaticData> constDataEntries;
  Array<StaticData> mutableDataEntries;
  Array<char> constData;
  Array<char> mutableData;

  void computeDominators();
};

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
  NOP,
  BLOCK_HEADER,
  DATA_HEADER,
  BYTES,
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

namespace local {
struct Reference : TypedStruct<uint, 1> {
  TypedRef target() const { return p[p.data(i)]; }
};
}  // namespace local

struct Invalid : TypedStruct<uint, 1> {
  TypedRef init() const { return init_(INVALID, 0); }
};
struct Nop : TypedStruct<uint, 1> {
  TypedRef init() const { return init_(NOP, 0); }
};
struct BlockHeader : local::Reference {
  TypedRef init(Block* blocks, Block& block) const {
    return init_(BLOCK_HEADER, blocks[block.dominator].events.bound);
  }
};
struct DataHeaderPayload {
  uint logElementSize : 4;
  uint numElements : 28;
};
struct DataHeader : TypedStruct<DataHeaderPayload, 1> {
  TypedRef init(DataHeaderPayload payload) const {
    return init_(DATA_HEADER, payload);
  }
};
struct Bytes : TypedStruct<uint, 1>{
  TypedRef init(uint bytes) const { return init_(BYTES, bytes); }
};
struct Zero : TypedStruct<uint, 1> {
  TypedRef init() const { return init_(ZERO, 0); }
};
struct UndefinedValue : TypedStruct<uint, 1>{
  TypedRef init() const { return init_(UNDEFINED_VALUE, 0); }
};
struct StaticAddress : TypedStruct<uint, 1> {
  TypedRef init(uint label) const {
    return init_(STATIC_ADDRESS, label);
  }
};
struct Use : local::Reference {
  TypedRef init(uint target) const { return init_(USE, target); }
};
struct Phi : local::Reference {
  TypedRef init() const { return init_(PHI, (uint)i); }
};
struct PhiArgument : TypedStruct<uint, 2> {
  Use arg() const { return p[i + 1].as<Use>(); }
  Phi phi() const { return p[p.data(i)].as<Phi>(); }
  TypedRef init(uint source, uint phi) const {
    arg().init(source);
    return init_(PHI_ARGUMENT, phi);
  }
};
struct Call : TypedStruct<uint, 2> {
  uint& numArgs() const { return p.data(i); }
  TypedStruct callee() const { return p[i + i]; }
  Use arg(size_t j) const { return field<Use>(i + 2 + j); }
  TypedRef init(Code targetCode, uint target, uint numArgs) const {
    callee().type(targetCode).data(target);
    return type(CALL).data(numArgs).next();
  }
};
struct CallSPMD : TypedStruct<uint, 3> {
  uint& numArgs() const { return p.data(i); }
  TypedStruct callee() const { return p[i + i]; }
  uint& workCount() const { return p.data(i + 2); }
  Use arg(size_t j) const { return field<Use>(i + 3 + j); }
  TypedRef init(Code targetCode, uint target, uint numArgs,
                uint workCount) const {
    callee().type(targetCode).data(target);
    p[i + 2].as<TypedStruct>().type(BYTES).data(workCount);
    return type(CALL).data(numArgs).next();
  }
};
struct Return : local::Reference {
  // The return target is the base? pointer.
  TypedRef init(uint target) const { return type(RETURN).data(target).next(); }
};
struct Jump : TypedStruct<uint, 2> {
  TypedStruct target() const { return field<TypedStruct>(i + 1); }
  TypedRef init(Code targetCode, uint target) const {
    this->target().type(targetCode).data(target);
    // jump data is unused.
    return type(JUMP).data(0).next(); }
};
struct Branch : TypedStruct<uint, 4> {
  Use arg() const { return field<Use>(i + 1); }
  StaticAddress target0() const { return field<StaticAddress>(i + 2); }
  StaticAddress target1() const { return field<StaticAddress>(i + 3); }
  TypedRef init(uint arg, uint target0, uint target1) const {
    this->arg().init(arg);
    this->target0().init(target0);
    this->target1().init(target1);
    // branch data is unused.
    return type(BRANCH).data(0).next();
  }
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

namespace local {
template <typename Payload>
struct Unary : TypedStruct<Payload, 2> {
  Use arg() const { return field<Use>(i + 1); }
};
template <typename Payload>
struct Binary : TypedStruct<Payload, 3> {
  Use arg0() const { return field<Use>(i + 1); }
  Use arg1() const { return field<Use>(i + 2); }
};

struct TypedUnary : Unary<TypedPayload> {
  TypedRef init(Code code, TypedPayload payload, uint value) const {
    arg().init(value);
    return data(payload).type(code).next();
  }
};
}  // namespace local

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
  local::Address address() const { return field<local::Address>(i + 1); }
};
struct Load : TypedStruct<LoadStorePayload, 2> {
  local::Address address() const { return field<local::Address>(i + 1); }
};
struct Store : TypedStruct<LoadStorePayload, 3> {
  local::Address address() const { return field<local::Address>(i + 1); }
  Use arg() const { return field<Use>(i + 2); }
};
struct MemSet : TypedStruct<MemOpPayload, 4> {
  local::Address address() const { return field<local::Address>(i + 1); }
  Use value() const { return field<Use>(i + 2); }
  Use size() const { return field<Use>(i + 3); }
};
struct MemCopy : TypedStruct<MemOpPayload, 4> {
  local::Address dst() const { return field<local::Address>(i + 1); }
  local::Address src() const { return field<local::Address>(i + 2); }
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
struct Extract : local::Unary<ExtractInsertPayload> {};
struct Insert : TypedStruct<ExtractInsertPayload, 3> {
  Use scalarArg() const { return field<Use>(i + 1); }
  Use vectorArg() const { return field<Use>(i + 2); }
};
struct BroadCast : local::Unary<TypedPayload> {};
struct Permute : local::Unary<ShufflePayload> {};
struct Shuffle : local::Binary<ShufflePayload> {};

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
struct BitTest : local::Unary<BitTestPayload> {};
struct Not : local::Unary<TypedPayload> {};
struct Logic : local::Binary<LogicPayload> {};
struct Logic3 : TypedStruct<Logic3Payload, 4>{
  Use arg0() const { return field<Use>(i + 1); }
  Use arg1() const { return field<Use>(i + 2); }
  Use arg2() const { return field<Use>(i + 3); }
};
struct Shift : local::Binary<ShiftPayload> {};
struct BitfieldExtract : local::Unary<BitFieldPayload> {};
struct BitfieldInsert : TypedStruct<BitFieldPayload, 3> {
  Use target() const { return field<Use>(i + 1); }
  Use source() const { return field<Use>(i + 2); }
};
struct BitfieldClear : local::Unary<BitFieldPayload> {};
struct CountZeros : local::Unary<CountZerosPayload> {};
struct PopCnt : local::Unary<TypedPayload>{};

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
struct Compare : local::Binary<ComparePayload> {};
struct Min : local::Binary<TypedPayload> {};
struct Max : local::Binary<TypedPayload> {};
struct Neg : local::Unary<TypedPayload> {};
struct Abs : local::Unary<TypedPayload> {};
struct Add : local::Binary<TypedPayload> {};
struct Sub : local::Binary<TypedPayload> {};
struct Mul : local::Binary<TypedPayload> {};
struct Div : local::Binary<TypedPayload> {};

//==============================================================================
// Integer math opcodes.
//==============================================================================

struct Mulhi : local::Binary<TypedPayload> {};
struct Mod : local::Binary<TypedPayload> {};

//==============================================================================
// Floating point math operations.
//==============================================================================

struct RoundPayload {
  enum Mode { EVEN, UP, DOWN, TRUNC, CURRENT } mode;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Rcp : local::Unary<TypedPayload> {};
struct Sqrt : local::Unary<TypedPayload> {};
struct Rsqrt : local::Unary<TypedPayload> {};
struct Exp2 : local::Unary<TypedPayload> {};
struct Round : local::Unary<RoundPayload> {};
struct Convert : local::Unary<TypedPayload> {};
struct Fixup : TypedStruct<TypedPayload, 3> {
  Bytes control() const { return field<Bytes>(i + 1); }
  Use arg() const { return field<Use>(i + 2); }
};

//==============================================================================
// Atomic operations.
//==============================================================================

struct AtomicXchg : Store {};
struct AtomicCompareXchg : TypedStruct<TypedPayload, 4> {
  local::Address address() const { return field<local::Address>(i + 1); }
  Use value() const { return field<Use>(i + 2); }
  Use comparand() const { return field<Use>(i + 3); }
};
struct AtomicLogicXchg : Store {};
struct AtomicAddXchg : Store {};
struct AtomicSubXchg : Store {};
}  // namespace wax
}  // namespace jagger
