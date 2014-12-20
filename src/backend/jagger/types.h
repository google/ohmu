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
namespace Fez {

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

struct SlotRef : TypedRef {
  TypedRef target() const { return TypedRef(p, p.data(i)); }
};

struct BinaryData : TypedRef {
  uint& data() const { return p.data(i); }
};

struct HasResult : TypedRef {
  Type& type() const { return ((Type*)&p.data(i))[3]; }
};

struct UnaryOp : HasResult {
  Use arg() const { return TypedRef(p, i - 1).as<Use>(); }
  static size_t slotCount() { return 2; }
};

struct BinaryOp : HasResult {
  Use arg0() const { return TypedRef(p, i - 1).as<Use>(); }
  Use arg1() const { return TypedRef(p, i - 2).as<Use>(); }
  static size_t slotCount() { return 3; }
};

struct BitfieldOp : UnaryOp {
  uchar& first() const { return ((uchar*)&p.data(i))[2]; }
  uchar& last() const { return ((uchar*)&p.data(i))[1]; }
};

struct MemoryOp : TypedRef {
  Use address() const { return TypedRef(p, i - 1).as<Use>(); }
  LinkAddress link() const { return TypedRef(p, i - 1).as<LinkAddress>(); }
  bool isLink() const { return p.type(i - 1) == LINK_ADDRESS; }
  static size_t slotCount() { return 2; }
};

struct ResultMemoryOp : MemoryOp {
  Type& type() const { return ((Type*)&p.data(i))[3]; }
};

enum Opcode {
  NOP,
  CASE_HEADER,
  JOIN_HEADER,
  USE,
  PHI,
  JOIN_COPY,
  LINK_ADDRESS,

  CALL,
  CALL_SPMD,
  RETURN,
  JUMP,
  INDIRECT_JUMP,
  BRANCH,

  BYTES,
  ALIGNED_BYTES,
  ZERO,
  UNDEFINED,

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

  COMPARE,
  BIT_TEST,
  NOT,
  LOGIC,
  LOGIC3,
  SHIFT,
  MIN,
  MAX,
  NEG,
  ADD,
  SUB,
  ADDR,
  MUL,
  MULHI,
  DIV,
  MOD,
  ABS,
  RCP,
  SQRT,
  RSQRT,
  EXP2,
  ROUND,
  CONVERT,
  FIXUP,

  BITFIELD_EXTRACT,
  BITFIELD_INSERT,
  BITFIELD_CLEAR,
  COUNT_ZEROS,
  POPCNT,

  ATOMIC_XCHG,
  ATOMIC_COMPARE_XCHG,
  ATOMIC_LOGIC_XCHG,
  ATOMIC_ADD_XCHG,

  NUM_OPCODES,
};

struct Nop : BinaryData {};
struct CaseHeader: SlotRef {};
struct JoinHeader: SlotRef {};
struct Use : SlotRef {};
struct Phi : SlotRef {};
struct JoinCopy : TypedRef {
  Use arg() const { return TypedRef(p, i - 1).as<Use>(); }
  Phi phi() const { return TypedRef(p, p.data(i)).as<Phi>(); }
  static size_t slotCount() { return 2; }
};
struct LinkAddress : SlotRef {};

struct Call : MemoryOp {
  Use arg(size_t j) const { return TypedRef(p, i - 2 - j).as<Use>(); }
  uint& numArgs() const { return p.data(i); }
  static size_t slotCount(size_t numArgs) { return 2 + numArgs; }
};
struct CallSPMD : MemoryOp {
  Use workCount() const { return TypedRef(p, i - 2).as<Use>(); }
  Use arg(size_t j) const { return TypedRef(p, i - 3 - j).as<Use>(); }
  uint& numArgs() const { return p.data(i); }
  static size_t slotCount(size_t numArgs) { return 3 + numArgs; }
};
struct Return : TypedRef {};
struct Jump : SlotRef {};
struct IndirectJump : SlotRef {};
struct Branch : UnaryOp {
  LinkAddress target(size_t j) const {
    return TypedRef(p, i - 2 - j).as<LinkAddress>();
  }
  uint& numTargets() const { return p.data(i); }
  static size_t slotCount(size_t numTargets) { return 2 + numTargets; }
};

struct AlignedBytes : BinaryData {};
struct Bytes : BinaryData {};
struct Zero : HasResult {};
struct Undefined : HasResult {};

struct Prefetch : MemoryOp {
  enum Kind { NT, L1, L2, L3 };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Load : ResultMemoryOp {
  enum Kind { NORMAL, NON_TEMPORAL, UNALIGNED };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Store : MemoryOp {
  enum Kind { NORMAL, NON_TEMPORAL, UNALIGNED };
  Use value() const { return TypedRef(p, i - 2).as<Use>(); }
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
  static size_t slotCount() { return 3; }
};
struct MemSet : MemoryOp {
  Use size() const { return TypedRef(p, i - 2).as<Use>(); }
  Use value() const { return TypedRef(p, i - 3).as<Use>(); }
  uchar& alignment() const { return ((uchar*)&p.data(i))[2]; }
  static size_t slotCount() { return 4; }
};
struct MemCopy : MemoryOp {
  Use size() const { return TypedRef(p, i - 2).as<Use>(); }
  Use source() const { return TypedRef(p, i - 3).as<Use>(); }
  uchar& alignment() const { return ((uchar*)&p.data(i))[2]; }
  static size_t slotCount() { return 4; }
};

struct Extract : UnaryOp {
  enum Kind { SLOT_0, SLOT_1, SLOT_2, SLOT_3 };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Insert : HasResult {
  enum Kind { SLOT_0, SLOT_1, SLOT_2, SLOT_3 };
  Use vector() const { return TypedRef(p, i - 1).as<Use>(); }
  Use scalar() const { return TypedRef(p, i - 2).as<Use>(); }
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
  static size_t slotCount() { return 3; }
};
struct BroadCast : UnaryOp {};
struct Permute : UnaryOp {
  uchar& control() const { return ((uchar*)&p.data(i))[2]; }
};
struct Shuffle : BinaryOp {
  uchar& control0() const { return ((uchar*)&p.data(i))[2]; }
  uchar& control1() const { return ((uchar*)&p.data(i))[1]; }
};

struct Compare : BinaryOp {
  enum Kind {
    FALSE, LT, EQ, LE, GT, NEQ, GE, ORD,
    UNORD, LTU, EQU, LEU, GTU, NEQU, GEU, TRUE,
  };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct BitTest : UnaryOp {
  enum Kind { READ, CLEAR, SET, TOGGLE };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Not : UnaryOp {};
struct Logic : BinaryOp {
  enum Kind : uchar {
    FALSE, NOR, GT, NOTB, LT, NOTA, XOR, NAND,
    AND, EQ, A, GE, B, LE, OR, TRUE,
  };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Logic3 : BinaryOp {
  uchar& kind() const { return ((uchar*)&p.data(i))[2]; }
};
struct Shift : BinaryOp {
  enum Kind { SHIFT, RIGHT = 0x00, LEFT, ROTATE, ARITHMETIC = 0x04 };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Min : BinaryOp {};
struct Max : BinaryOp {};
struct Neg : UnaryOp {};
struct Add : BinaryOp {};
struct Sub : BinaryOp {};
struct Addr : HasResult {
  Use base() const { return TypedRef(p, i - 1).as<Use>(); }
  Use index() const { return TypedRef(p, i - 2).as<Use>(); }
  Bytes disp() const { return TypedRef(p, i - 3).as<Bytes>(); }
  uchar& scale() const { return ((uchar*)&p.data(i))[2]; }
  static size_t slotCount() { return 4; }
};
struct Mul : BinaryOp {};
struct Mulhi : BinaryOp {};
struct Div : BinaryOp {};
struct Mod : BinaryOp {};
struct Abs : UnaryOp {};
struct Rcp : UnaryOp {};
struct Sqrt : UnaryOp {};
struct Rsqrt : UnaryOp {};
struct Exp2 : UnaryOp {};
struct Fixup : UnaryOp {};
struct Round : UnaryOp {
  enum Kind { EVEN, UP, DOWN, TRUNC, CURRENT };
  Kind& kind() const { return ((Kind*)&p.data(i))[2]; }
};
struct Convert : UnaryOp {
  Type& from() const { return ((Type*)&p.data(i))[2]; }
};
struct Fixup : HasResult {
  Use arg() const { return TypedRef(p, i - 1).as<Use>(); }
  Bytes control() const { return TypedRef(p, i - 2).as<Bytes>(); }
  static size_t slotCount() { return 3; }
};

struct BitfieldExtract : BitfieldOp {};
struct BitfieldInsert : BitfieldOp {
  Use input() const { return TypedRef(p, i - 2).as<Use>(); }
  static size_t slotCount() { return 3; }
};
struct BitfieldClear : BitfieldOp {};
struct CountZeros : UnaryOp {
  enum Kind { TRAILING, LEADING };
  Kind& scale() const { return ((Kind*)&p.data(i))[2]; }
};
struct PopCnt : UnaryOp {};

struct AtomicXchg : ResultMemoryOp {
  Use arg() const { return TypedRef(p, i - 2).as<Use>(); }
  static size_t slotCount() { return 3; }
};
struct AtomicCompareXchg : ResultMemoryOp {
  Use setTo() const { return TypedRef(p, i - 2).as<Use>(); }
  Use compareTo() const { return TypedRef(p, i - 3).as<Use>(); }
  static size_t slotCount() { return 4; }
};
struct AtomicLogicXchg : AtomicXchg {
  Logic::Kind& kind() const { return ((Logic::Kind*)&p.data(i))[2]; }
  static size_t slotCount() { return 3; }
};
struct AtomicAddXchg : AtomicXchg {};
struct AtomicSubXchg : AtomicXchg {};

}  // namespace Fez
}  // namespace Jagger
