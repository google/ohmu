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
    BINARY = 0x00,
    UNSIGNED = 0x04,
    INTEGER = 0x08,
    FLOAT = 0x0c,
    VOID = 0x10,
    BOOLEAN = 0x14,
    ADDRESS = 0x18,
    STACK = 0x1c,
  };
  enum Count : uchar { SCALAR = 0x00, VEC2 = 0x20, VEC3 = 0x30, VEC4 = 0x40 };
  enum Variance : uchar { VARYING = 0x00, UNIFORM = 0x80 };

  Type() {}
  Type(Kind kind, Size size = BYTE, Count count = SCALAR, Variance variance = VARYING)
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

struct Label {
  enum Kind : uint { ZERO, INITIALIZED, CONSTANT, CODE, TLS_ZERO, TLS_INITIALIZED };
  Label(Kind kind, uint size) : data((size << 3) + kind) {}
  Kind kind() const { return (Kind)(data & 0x7); }
  uint size() const { return data >> 3; }

private:
  uint data;
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
struct Bytes : TypedStruct<uint, 1> {
  TypedRef init(uint bytes) const { return init_(BYTES, bytes); }
};
struct Zero : TypedStruct<uint, 1> {
  TypedRef init() const { return init_(ZERO, 0); }
};
struct UndefinedValue : TypedStruct<uint, 1>{
  TypedRef init() const { return init_(UNDEFINED_VALUE, 0); }
};
struct StaticAddress : TypedStruct<Label, 1> {
  TypedRef init(Label label) const {
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
  Use arg() const { return field<Use>(1); }
  Phi phi() const { return p[p.data(i)].as<Phi>(); }
  TypedRef init(uint source, uint phi) const {
    arg().init(source);
    return init_(PHI_ARGUMENT, phi);
  }
};
struct Call : TypedStruct<uint, 3> {
  uint& numArgs() const { return p.data(i); }
  Use callee() const { return field<Use>(1); }
  Use stackPointer() const { return field<Use>(2); }
  Use arg(size_t j) const { return field<Use>(3 + j); }
  TypedRef init(uint target, uint numArgs, uint stackPointer) const {
    this->stackPointer().init(stackPointer);
    callee().init(target);
    return init_(CALL, numArgs);
  }
};
struct CallSPMD : TypedStruct<uint, 4> {
  uint& numArgs() const { return p.data(i); }
  Use callee() const { return field<Use>(1); }
  Use stackPointer() const { return field<Use>(2); }
  uint& workCount() const { return p.data(i + 3); }
  Use arg(size_t j) const { return field<Use>(4 + j); }
  TypedRef init(uint target, uint numArgs, uint stackPointer,
                uint workCount) const {
    p.type(i + 3) = BYTES;
    p.data(i + 3) = workCount;
    this->stackPointer().init(stackPointer);
    callee().init(target);
    return init_(CALL, numArgs);
  }
};
struct Return : TypedStruct<uint, 1> {
  // TODO: X64 ret takes ESP/EBP in
  uint& numArgs() const { return p.data(i); }
  TypedRef init(uint numArgs) const { return init_(RETURN, numArgs); }
};
struct Jump : TypedStruct<uint, 2> {
  Use target() const { return field<Use>(1); }
  TypedRef init(uint target) const {
    this->target().init(target);
    return init_(JUMP, 0);
  }
};
struct Branch : TypedStruct<uint, 4> {
  Use arg() const { return field<Use>(1); }
  Use target0() const { return field<Use>(2); }
  Use target1() const { return field<Use>(3); }
  TypedRef init(uint arg, uint target0, uint target1) const {
    this->arg().init(arg);
    this->target0().init(target0);
    this->target1().init(target1);
    return init_(BRANCH, 0);
  }
};
struct Switch : TypedStruct<uint, 2> {
  uint& numTargets() const { return p.data(i); }
  Use arg() const { return field<Use>(1); }
  Use target(size_t j) const { return field<Use>(2 + j); }
  TypedRef init(uint arg, uint numTargets) const {
    this->arg().init(arg);
    return init_(SWITCH, numTargets);
  }
};

//==============================================================================
// Helper types.
//==============================================================================

namespace local {
template <Code code, typename Payload>
struct Unary : TypedStruct<Payload, 2> {
  Use arg() const { return field<Use>(1); }
  TypedRef init(Payload payload, uint arg) const {
    this->arg().init(arg);
    return init_(code, payload);
  }
};
template <Code code, typename Payload>
struct Binary : TypedStruct<Payload, 3> {
  Use arg0() const { return field<Use>(1); }
  Use arg1() const { return field<Use>(2); }
  TypedRef init(Payload payload, uint arg0, uint arg1) const {
    this->arg0().init(arg0);
    this->arg1().init(arg1);
    return init_(code, payload);
  }
};
}  // namespace local

//==============================================================================
// Memory opcodes.
//==============================================================================

struct ComputeAddressPayload {
  ComputeAddressPayload() {}
  ComputeAddressPayload(Type type, uchar scale) : scale(scale), type(type) {}
  uchar scale;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct PrefetchPayload {
  enum Kind : uint { NT, L1, L2, L3 } kind;
};
struct LoadStorePayload {
  LoadStorePayload() {}
  LoadStorePayload(Type type, uchar flags = 0) : flags(flags), type(type) {}
  enum Flags : uchar { NON_TEMPORAL = 0x01, UNALIGNED = 0x02 };
  uchar flags;
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
  Use target() const { return field<Use>(1); }
  TypedRef init(PrefetchPayload payload, uint target) const {
    this->target().init(target);
    return init_(PREFETCH, payload);
  }
};
struct Load : TypedStruct<LoadStorePayload, 2> {
  Use target() const { return field<Use>(1); }
  TypedRef init(LoadStorePayload payload, uint target) const {
    this->target().init(target);
    return init_(LOAD, payload);
  }
};
struct Store : TypedStruct<LoadStorePayload, 3> {
  Use target() const { return field<Use>(1); }
  Use arg() const { return field<Use>(2); }
  TypedRef init(LoadStorePayload payload, uint target, uint arg) const {
    this->target().init(target);
    this->arg().init(arg);
    return init_(STORE, payload);
  }
};
struct MemSet : TypedStruct<MemOpPayload, 4> {
  Use target() const { return field<Use>(1); }
  Use value() const { return field<Use>(2); }
  Use numElements() const { return field<Use>(3); }
  TypedRef init(MemOpPayload payload, uint target, uint value,
                uint numElements) const {
    this->target().init(target);
    this->value().init(value);
    this->numElements().init(numElements);
    return init_(MEM_SET, payload);
  }
};
struct MemCopy : TypedStruct<MemOpPayload, 4> {
  Use target() const { return field<Use>(1); }
  Use source() const { return field<Use>(2); }
  Use numElements() const { return field<Use>(3); }
  TypedRef init(MemOpPayload payload, uint target, uint source,
                uint numElements) const {
    this->target().init(target);
    this->source().init(source);
    this->numElements().init(numElements);
    return init_(MEM_SET, payload);
  }
};

//==============================================================================
// Explicitly SIMD opcodes.
//==============================================================================

struct TypedPayload {
  TypedPayload() {}
  TypedPayload(Type type) : type(type) {}
  uchar : 8;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ExtractInsertPayload {
  ExtractInsertPayload() {}
  ExtractInsertPayload(Type type, uchar lane) : lane(lane), type(type) {}
  uchar lane;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ShufflePayload {
  ShufflePayload() {}
  ShufflePayload(Type type, uchar lane0, uchar lane1, uchar lane2, uchar lane3)
      : lane0(lane0), lane1(lane1), lane2(lane2), lane3(lane3), type(type) {}
  uchar lane0 : 4;
  uchar lane1 : 4;
  uchar lane2 : 4;
  uchar lane3 : 4;
  uchar : 8;
  Type type;
};
struct Extract : local::Unary<EXTRACT, ExtractInsertPayload> {};
struct Insert : TypedStruct<ExtractInsertPayload, 3> {
  Use scalarArg() const { return field<Use>(i + 1); }
  Use vectorArg() const { return field<Use>(i + 2); }
  TypedRef init(ExtractInsertPayload payload, uint scalarArg,
                uint vectorArg) const {
    this->scalarArg().init(scalarArg);
    this->vectorArg().init(vectorArg);
    return init_(INSERT, payload);
  }
};
struct BroadCast : local::Unary<BROADCAST, TypedPayload> {};
struct Permute : local::Unary<PERMUTE, ShufflePayload> {};
struct Shuffle : local::Binary<SHUFFLE, ShufflePayload> {};

//==============================================================================
// Bit opcodes.
//==============================================================================

struct BitTestPayload {
  enum Kind : uchar { READ, CLEAR, SET, TOGGLE };
  BitTestPayload() {}
  BitTestPayload(Type type, Kind kind) : type(type), kind(kind) {}
  Kind kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct LogicPayload {
  enum Kind : uchar {
    FALSE, NOR, GT, NOTB, LT, NOTA, XOR, NAND,
    AND, EQ, A, GE, B, LE, OR, TRUE,
  };
  LogicPayload() {}
  LogicPayload(Type type, Kind kind) : type(type), kind(kind) {}
  Kind kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Logic3Payload {
  Logic3Payload() {}
  Logic3Payload(Type type, uchar kind) : type(type), kind(kind) {}
  uchar kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct ShiftPayload {
  ShiftPayload() {}
  ShiftPayload(Type type, uchar flags = 0) : type(type), flags(flags) {}
  enum Flags : uchar {
    SHIFT = 0x00,
    RIGHT = 0x00,
    LEFT = 0x01,
    ROTATE = 0x02,
    ARITHMETIC = 0x04
  };
  uchar flags;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct BitFieldPayload {
  BitFieldPayload() {}
  BitFieldPayload(Type type, uchar begin, uchar end)
      : type(type), begin(begin), end(end) {}
  uchar begin;
  uchar end;
  uchar : 8;
  Type type;
};
struct CountZerosPayload {
  enum Kind : uchar { TRAILING, LEADING };
  CountZerosPayload() {}
  CountZerosPayload(Type type, Kind kind) : type(type), kind(kind) {}
  Kind kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct BitTest : local::Unary<BIT_TEST, BitTestPayload> {};
struct Not : local::Unary<NOT, TypedPayload> {};
struct Logic : local::Binary<LOGIC, LogicPayload> {};
struct Logic3 : TypedStruct<Logic3Payload, 4>{
  Use arg0() const { return field<Use>(1); }
  Use arg1() const { return field<Use>(2); }
  Use arg2() const { return field<Use>(3); }
  TypedRef init(Logic3Payload payload, uint arg0, uint arg1, uint arg2) const {
    this->arg0().init(arg0);
    this->arg1().init(arg1);
    this->arg2().init(arg2);
    return init_(LOGIC3, payload);
  }
};
struct Shift : local::Binary<SHIFT, ShiftPayload> {};
struct BitfieldExtract : local::Unary<BITFIELD_EXTRACT, BitFieldPayload> {};
struct BitfieldInsert : TypedStruct<BitFieldPayload, 3> {
  Use target() const { return field<Use>(i + 1); }
  Use source() const { return field<Use>(i + 2); }
  TypedRef init(BitFieldPayload payload, uint target, uint source) const {
    this->target().init(target);
    this->source().init(source);
    return init_(BITFIELD_INSERT, payload);
  }
};
struct BitfieldClear : local::Unary<BITFIELD_CLEAR, BitFieldPayload> {};
struct CountZeros : local::Unary<COUNT_ZEROS, CountZerosPayload> {};
struct PopCnt : local::Unary<POPCNT, TypedPayload>{};

//==============================================================================
// Math opcodes.
//==============================================================================

struct ComparePayload {
  enum Kind : uchar {
    FALSE, LT, EQ, LE, GT, NEQ, GE, ORD,
    UNORD, LTU, EQU, LEU, GTU, NEQU, GEU, TRUE,
  };
  ComparePayload() {}
  ComparePayload(Type type, Kind kind) : type(type), kind(kind) {}
  Kind kind;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Compare : local::Binary<COMPARE, ComparePayload> {};
struct Min : local::Binary<MIN, TypedPayload> {};
struct Max : local::Binary<MAX, TypedPayload> {};
struct Neg : local::Unary<NEG, TypedPayload> {};
struct Abs : local::Unary<ABS, TypedPayload> {};
struct Add : local::Binary<ADD, TypedPayload> {};
struct Sub : local::Binary<SUB, TypedPayload> {};
struct Mul : local::Binary<MUL, TypedPayload> {};
struct Div : local::Binary<DIV, TypedPayload> {};

//==============================================================================
// Integer math opcodes.
//==============================================================================

struct Mulhi : local::Binary<MULHI, TypedPayload> {};
struct Mod : local::Binary<MOD, TypedPayload> {};

//==============================================================================
// Floating point math operations.
//==============================================================================

struct RoundPayload {
  enum Mode { EVEN, UP, DOWN, TRUNC, CURRENT };
  RoundPayload() {}
  RoundPayload(Type type, Mode mode) : type(type), mode(mode) {}
  Mode mode;
  uchar : 8;
  uchar : 8;
  Type type;
};
struct Rcp : local::Unary<RCP, TypedPayload> {};
struct Sqrt : local::Unary<SQRT, TypedPayload> {};
struct Rsqrt : local::Unary<RSQRT, TypedPayload> {};
struct Exp2 : local::Unary<EXP2, TypedPayload> {};
struct Round : local::Unary<ROUND, RoundPayload> {};
struct Convert : local::Unary<CONVERT, TypedPayload> {};
struct Fixup : TypedStruct<TypedPayload, 3> {
  Bytes control() const { return field<Bytes>(1); }
  Use arg() const { return field<Use>(2); }
  TypedRef init(TypedPayload payload, uint control, uint arg) const {
    this->control().init(control);
    this->arg().init(arg);
    return init_(FIXUP, payload);
  }
};

//==============================================================================
// Atomic operations.
//==============================================================================

struct AtomicXchg : Store {};
struct AtomicCompareXchg : TypedStruct<TypedPayload, 4> {
  Use target() const { return field<Use>(1); }
  Use value() const { return field<Use>(2); }
  Use comparand() const { return field<Use>(3); }
  TypedRef init(TypedPayload payload, uint target, uint value, uint comparand) {
    this->target().init(target);
    this->value().init(value);
    this->comparand().init(comparand);
    return init_(ATOMIC_COMPARE_XCHG, payload);
  }
};
struct AtomicLogicXchg : Store {};
struct AtomicAddXchg : Store {};
struct AtomicSubXchg : Store {};
}  // namespace wax
}  // namespace jagger
