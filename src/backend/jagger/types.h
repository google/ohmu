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

namespace ohmu {
namespace til {
class BasicBlock;
}
}

namespace Jagger {
//namespace {

#if 0
enum {
  NOP = 0, USE, REF, GOTO_HEADER, WALK_HEADER,
  HALT, IMM8, IMM16, IMM32, IMM64,
  LOAD, STORE, PREFETCH, CONVERT, CMOV,
  AND, OR, ANDN, ORN, XOR, XNOR, NAND, NOR, NOT,
  SLL, SLR, SAR, ROL, ROR,
  ADD, SUB, NEG, ABS,
  MUL, MULHI, DIV, MOD,
  AOS, AOSOA,
  BT, BTS, BTR, BTC,
  CTZ, CLZ, POPCNT, /* other bit ops bmi1/bmi2 */
  KLOGIC, KSHUFFLE,
  VMOVEMASK, VMASK, VLOAD, VSTORE,
  VGATHER, VSCATTER, VPREFETCH,
  VNEG, VADD, VSUB, VMUL, VMULHI, VDIV,
  FMADD,
  VSQRT, VRSQRT, VRCP, VEXP2,
  VMIN, VMAX, VAVG,
  VLOGIC, VLOGIC3,
  VSLL, VSLR, VSAR, VROL, VROR,
  VCONVERT, COMPARE,
  VPERMUTE, VSHUFFLE,
  VCOMPRESS, VEXPAND,
#if 0
  INT32, LOAD, STORE, ULOAD, USTORE, GATHER, SCATTER,
  SEXT, ZEXT, FCVT, /* ZEXT/FCVT used for truncation */
  AND, OR, ANDN, ORN, XOR, XNOR, NAND, NOR, NOT,
  SLL, SLR, SAR, ROL, ROR,
  MIN, MAX,
  ADD, SUB, SUBR, ADDN, ADC, SBB, NEG, ABS,
  MUL, MULHI, DIV, MOD, RCP,
  AOS, AOSOA,
  MADD, MSUB, MSUBR, MADDN,
  FMADD, FMSUB, FMSUBR, FMADDN,
  EQ, NEQ, LT, LE, ORD, EQU, NEQU, LTU, LEU, UNORD,
  JUMP, BRANCH, CALL, RET,
  BT, BTS, BTR, BTC,
  CTZ, CLZ, POPCNT, /* other bit ops bmi1/bmi2 */
  SQRT, RSQRT,
  SHUFFLE, BROADCAST, EXTRACT, INSERT,
  MEMSET, MEMCPY,
#endif
};
#endif

typedef unsigned char Opcode;
typedef unsigned int uint;
typedef unsigned char uchar;

enum Type : unsigned char {
  BINARY_DATA, UNSIGNED_INTEGER, SIGNED_INTEGER, FLOAT
};

enum LogBits : unsigned char {
  LOG1, LOG8 = 3, LOG16, LOG32, LOG64, LOG128
};

enum VectorWidth : unsigned char {
  VEC1, VEC2, VEC4, VEC8, VEC16, VEC32, VEC64, VEC128
};

struct TypeDesc {
  TypeDesc(Type type, LogBits logBits, VectorWidth vectorWidth = VEC1)
      : type(type), logBits(logBits), vectorWidth(vectorWidth) {}
  operator uchar() const { return *(uchar*)this; }
  uchar type : 2;
  uchar logBits : 3;
  uchar vectorWidth : 3;
 private:
  TypeDesc operator=(const TypeDesc&);
};

enum Opcodes {
  NOP,  
  ISA_OP,
  JOIN_COPY,
  CLOBBER_LIST,
  REGISTER_HINT,
  USE,
  INFERIOR_USE,
  VALUE_KEY,
  PHI,
  DESTRUCTIVE_VALUE = PHI + 8,
  VALUE = DESTRUCTIVE_VALUE + 8,
  GOTO_HEADER = VALUE + 8,
  WALK_HEADER,
  BYTES1, BYTES2, BYTES4, BYTES_HEADER, ALIGNED_BYTES, BYTES,
  CALL, RET,
  JUMP, BRANCH, BRANCH_TARGET,

  COMPARE,
  NOT, LOGIC, LOGIC3,
  BITFIELD_EXTRACT, BITFIELD_INSERT, BITFIELD_CLEAR,
  COUNT_ZEROS,
  MIN, MAX,
  ADD, SUB, NEG, ADDR,
  MUL, DIV, IMULHI, IDIV, IMOD,
  ABS, RCP, SQRT, RSQRT, EXP2,
  CONVERT,
  FIXUP,
  SHUFFLE, IGNORE_LANES, ZERO_LANES,
  PREFETCH,
  LOAD, GATHER, INSERT, EXPAND,
  STORE, SCATTER, EXTRACT, COMPRESS,
};

enum Compare {
  CMP_FALSE,
  CMP_LT,
  CMP_EQ,
  CMP_LE,
  CMP_GT,
  CMP_NEQ,
  CMP_GE,
  CMP_ORD,
  CMP_UNORD,
  CMP_LTU,
  CMP_EQU,
  CMP_LEU,
  CMP_GTU,
  CMP_NEQU,
  CMP_GEU,
  CMP_TRUE,
};

enum Logic {
  LOGICAL_FALSE,
  LOGICAL_NOR,
  LOGICAL_GT,
  LOGICAL_NOTB,
  LOGICAL_LT,
  LOGICAL_NOTA,
  LOGICAL_XOR,
  LOGICAL_NAND,
  LOGICAL_AND,
  LOGICAL_EQ,
  LOGICAL_A,
  LOGICAL_GE,
  LOGICAL_B,
  LOGICAL_LE,
  LOGICAL_OR,
  LOGICAL_TRUE,
};

struct BasicData {
  explicit BasicData(uchar type) : type(type) {}
  operator uint() const { return *(uint*)this; }
  uint type : 8;
};

struct ClobberListData {
  ClobberListData(uchar reg0, uchar reg1 = 0, uchar reg2 = 0, uchar reg3 = 0)
      : reg0(reg0), reg1(reg1), reg2(reg2), reg3(reg3) {}
  operator uint() const { return *(uint*)this; }
  uint reg0 : 3;
  uint reg1 : 3;
  uint reg2 : 3;
  uint reg3 : 3;
};

struct CompareData {
  CompareData(uchar type, Compare kind) : type(type), kind(kind) {}
  operator uint() const { return *(uint*)this; }
   uint type : 8;
   uint kind : 4;
};

struct LogicData {
  LogicData(uchar type, Logic kind) : type(type), kind(kind) {}
  operator uint() const { return *(uint*)this; }
   uint type : 8;
   uint kind : 4;
};

struct Logic3Data {
  Logic3Data(uchar type, uchar kind) : type(type), kind(kind) {}
  operator uint() const { return *(uint*)this; }
  uint type : 8;
  uint kind : 8;
};

struct ConvertData {
  ConvertData(uchar resultType, uchar sourceType)
      : resultType(resultType), sourceType(sourceType) {}
  operator uint() const { return *(uint*)this; }
  uint resultType : 8;
  uint sourceType : 8;
};

struct AddrData {
  AddrData(uchar type, uchar scale) : type(type), scale(scale) {}
  operator uint() const { return *(uint*)this; }
  uint type : 8;
  uint scale : 8;
};

struct BitfieldManipData {
  BitfieldManipData(uchar type, uchar start, uchar size)
    : type(type), start(start), size(size) {}
  operator uint() const { return *(uint*)this; }
  uint type : 8;
  uint start : 8;
  uint size : 8;
};

struct EventBuilder {
  explicit EventBuilder(char* root) : root(root) {}

  size_t use(size_t i, uint arg0) { return op(i, USE, arg0); }
  size_t hint(size_t i, uint index) { return op(i, REGISTER_HINT, index); }

  inline size_t op(size_t i, Opcode code, uint data);

  Opcode& code(size_t i) const { return ((Opcode*)root)[i]; }
  uint& data(size_t i) const { return ((uint*)root)[i]; }
  char* root;
};

size_t EventBuilder::op(size_t i, Opcode code, uint data) {
  if (!root) return i + 1;
  this->code(i) = code;
  this->data(i) = data;
  return i + 1;
}


#if 0
union Event {
  Event() {}
  Event(unsigned char code, unsigned data) : code(code), data(data) {}

  static inline size_t initNop(Event* events, size_t i, uint payload = 0);
  static inline size_t initGotoHeader(Event* events, size_t i, uint target);
  static inline size_t initWalkHeader(Event* events, size_t i, uint target);
  static inline size_t initPhi(Event* events, size_t i);
  static inline size_t initPhiCopy(Event* events, size_t i, uint arg0, uint phi);
  static inline size_t initJump(Event* events, size_t i, uint target);
  static inline size_t initBranch(Event* events, size_t i, uint arg0, uint thenTarget, uint elseTarget);
  static inline size_t initRet(Event* events, size_t i, uint arg0);
  static inline size_t initIntLiteral(Event* events, size_t i, int value);
  static inline size_t initAdd(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initSub(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initMul(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initEq(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initLt(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initLe(Event* events, size_t i, uint arg0, uint arg1);

  unsigned bits;
  struct {
    unsigned char : 8;
    unsigned char : 8;
    unsigned char : 8;
    unsigned char code : 8;
  };
  struct {
    unsigned data : 24;
    unsigned : 8;
  };
  struct {
    char offset0;
    char offset1;
    unsigned char reg0 : 4;
    unsigned char reg0 : 4;
  } fixed;
};
#endif

#if 0
struct Fixed {
  char offset0;
  char offset1;
  unsigned char reg : 3;
  unsigned char aliasSet : 2;
  unsigned char : 3;
};

struct Event {
  enum AliasSet { GPR = 1, FLAGS, X87, VPU, MASK };
  enum {
    VALUE = 0x80,
    FIXED = VALUE | 0x40,
    COPY = VALUE | 0x20,
    PHI = VALUE | 0x10,
    PHI_COPY = PHI | COPY,
    EAX = GPR,
    EDX = GPR | 0x8,
  };

  Event() {}
  Event(unsigned char code, unsigned data) : code(code), data(data) {}

  static inline size_t initNop(Event* events, size_t i, uint payload = 0);
  static inline size_t initGotoHeader(Event* events, size_t i, uint target);
  static inline size_t initWalkHeader(Event* events, size_t i, uint target);
  static inline size_t initPhi(Event* events, size_t i);
  static inline size_t initPhiCopy(Event* events, size_t i, uint arg0, uint phi);
  static inline size_t initJump(Event* events, size_t i, uint target);
  static inline size_t initBranch(Event* events, size_t i, uint arg0, uint thenTarget, uint elseTarget);
  static inline size_t initRet(Event* events, size_t i, uint arg0);
  static inline size_t initIntLiteral(Event* events, size_t i, int value);
  static inline size_t initAdd(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initSub(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initMul(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initEq(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initLt(Event* events, size_t i, uint arg0, uint arg1);
  static inline size_t initLe(Event* events, size_t i, uint arg0, uint arg1);

  union {
    unsigned bits;
    unsigned char code;
    struct {
      unsigned char aliasSet : 3;
      unsigned char : 1;
      unsigned char isPhiKind : 1;
      unsigned char isCopy : 1;
      unsigned char isFixed : 1;
      unsigned char isValue : 1;

      unsigned char type : 2;
      unsigned char size : 2;
      unsigned char count : 3;
      unsigned char : 1;
    };
    struct {
      unsigned char /*aliasSet*/ : 3;
      unsigned char fixedReg : 3;
      unsigned char /*isFixed = 1*/ : 1;
      unsigned char /*isValue = 1*/ : 1;
    };
    struct {
      unsigned : 8;
      unsigned data : 24;
    };
    struct {
      unsigned short : 16;
      unsigned char required : 1;
      union {
        struct {
          unsigned char aligned : 1;
          unsigned char nonTemporal : 1;
        } load, store;
        struct {
          unsigned char roundingMode : 3;
          unsigned char type : 2;
          unsigned char size : 2;
        } convertFrom;
        struct {
          unsigned char operation : 8;
        } logic;
        struct {
          unsigned char roundingMode : 3;
        } math;
        struct {
          unsigned char roundingMode : 3;
          unsigned char sub : 1;
          unsigned char neg : 1;
        } fmadd;
        struct {
          unsigned char lt : 1; // 1 : maybe, 0 : false
          unsigned char eq : 1; // 1 : maybe, 0 : false
          unsigned char gt : 1; // 1 : maybe, 0 : false
          unsigned char unordered : 1;
          unsigned char signaling : 1;
        } compare;
      };
    };
  };
};
#endif

struct Block {
  static const size_t NO_DOMINATOR = (size_t)-1;
  ohmu::til::BasicBlock* basicBlock;
  Block* list;
  size_t numArguments;
  size_t dominator;
  size_t head;
  size_t firstEvent;
  size_t boundEvent;
  size_t phiSlot;
};

#if 0
void print_stream(EventStream events, size_t numInstrs);
void print_asm(EventStream events, size_t numInstrs);
void make_asm(EventStream events, size_t numEvents);
#endif

#if 0
// EQ_OQ(EQ) 0H Equal(ordered, non - signaling) False False True False No
// UNORD_Q(UNORD) 3H Unordered(non - signaling) False False False True No
// NEQ_UQ(NEQ) 4H Not - equal(unordered, nonsignaling) True True False True No
// ORD_Q(ORD) 7H Ordered(non - signaling) True True True False No
// EQ_UQ 8H Equal(unordered, non - signaling) False False True True No
// FALSE_OQ(FALSE) BH False(ordered, non - signaling) False False False False No
// NEQ_OQ CH Not - equal(ordered, non - signaling) True True False False No
// TRUE_UQ(TRUE) FH True(unordered, non - signaling) True True True True No
// LT_OQ 11H Less - than(ordered, nonsignaling) False True False False No
// LE_OQ 12H Less - than - or - equal(ordered, nonsignaling) False True True False No
// NLT_UQ 15H Not - less - than(unordered, nonsignaling) True False True True No
// NLE_UQ 16H Not - less - than - or - equal(unordered, nonsignaling) True False False True No
// NGE_UQ 19H Not - greater - than - or - equal(unordered, nonsignaling) False True False True No
// NGT_UQ 1AH Not - greater - than(unordered, nonsignaling) False True True True No
// GE_OQ 1DH Greater - than - or - equal(ordered, nonsignaling) True False True False No
// GT_OQ 1EH Greater - than(ordered, nonsignaling) True False False False No

// Instruction families

struct LOGICFamily {
  bool val00;
  bool val01;
  bool val10;
  bool val11;
};

struct ADDFamily {
  bool neg_result;
  bool neg_arg1;
  // round mode
};

struct MADFamily {
  bool neg_result;
  bool neg_arg1;
  bool fused;
  // round mode
};

struct COPYFamily {
  bool src_in_mem;
  bool dst_in_mem;
  bool unaligned;
};

struct CVTFamily { // sext, zext, fcvt
  bool signaling;
  // round mode
};

struct ShiftFamily {
  bool left;
  bool rotate;
  bool arithmetic;
};

struct CMPFamily {
  bool lt;
  bool eq;
  bool gt;
  bool unord;
};
#endif

//} //  namespace
} //  namespace Jagger
