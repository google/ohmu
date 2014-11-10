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

enum {
  NOP = 0, USE, MUTED_USE, GOTO_HEADER, WALK_HEADER,
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
};

typedef unsigned int uint;

struct Event {
  enum AliasSet { GPR = 1, FLAGS, MMX, SSE };
  enum {
    VALUE = 0x80,
    FIXED = VALUE | 0x40,
    COPY = VALUE | 0x20,
    PHI = VALUE | 0x10,
    PHI_COPY = PHI | COPY,
    EAX = GPR,
    EDX = GPR | 0x8,
  };

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
  };
};

static const size_t MAX_EVENTS = 1 << 24;

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
