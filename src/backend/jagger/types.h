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

#include "x64builder\x64builder.h"
namespace Jagger {

struct Block;
struct Instruction;
struct Use;

struct Opcode {
  enum {
    NOP, PHI, HEADER, TIE, COPY,
    VALUE, LOAD, STORE, GATHER, SCATTER,
    SEXT, ZEXT, FCVT, /* ZEXT/FCVT used for truncation */
    AND, OR, ANDN, ORN, XOR, XNOR, NOT,
    SLL, SLR, SAR, ROL, ROR,
    MIN, MAX,
    ADD, SUB, SUBR, ADDN, ADC, SBB, NEG, ABS,
    MUL, MULHI, DIV, MOD, RCP,
    LEA,
    MADD, MSUB, MSUBR, MADDN,
    FMADD, FMSUB, FMSUBR, FMADDN,
    EQ, LT, LE, UNORD, NEQ, NLT, NLE, ORD,
    JUMP, BRANCH, CALL, RET,
    BT, BTS, BTR, BTC,
    CTZ, CLZ, POPCNT, /* other bit ops bmi1/bmi2 */
    SQRT, RSQRT,
    SHUFFLE, BROADCAST, EXTRACT, INSERT,
  };
  //   
  // CRC, AES,
  // MOVEMASK,
  // RDTSC
  // MEMCPY, MEMSET
  // ATOMICS, EXCHAGE WITH MEMORY, CMPEXCH WITH MEMORY


  unsigned code : 8;

  unsigned hasResult : 1;
  unsigned isDestructive : 1; // always destroys argument 0
  unsigned isCommutative : 1;
  unsigned flags : 3; // header kind, rounding mode, ordering

  unsigned aliasSet : 2;
  unsigned type : 2;
  unsigned hasFixedReg : 1;
  unsigned fixedReg : 3;

  unsigned isArg0NotLastUse : 1;
  unsigned isArg1NotLastUse : 1;
  unsigned isUnused : 1;
  unsigned _ : 5;

  bool operator==(const Opcode& a) const { return *(int*)this == *(int*)this; }
  bool operator!=(const Opcode& a) const { return *(int*)this != *(int*)this; }
};

struct OpcodeEx : Opcode {
  unsigned hasFixedRegArg0 : 1;
  unsigned hasFixedRegArg1 : 1;
  unsigned registerFileArg0 : 3;
  unsigned registerFileArg1 : 3;
  unsigned fixedRegArg0 : 8;
  unsigned fixedRegArg1 : 8;
  const char name[16];
};

struct Instruction {
  void init(Opcode opcode) { init(opcode, this); }
  void init(Opcode opcode, Instruction* arg0) { init(opcode, arg0, this); }
  void init(Opcode opcode, Instruction* arg0, Instruction* arg1) {
    init(opcode, arg0, arg1, this);
  }
  void init(Opcode opcode, Instruction* arg0, Instruction* arg1,
            Instruction* key) {
    this->opcode = opcode;
    this->key = key - this;
    this->arg0 = arg0 - this;
    this->arg1 = arg1 - this;
    order = 0;
  }

  Instruction* getKey() { return this + key; }
  Instruction* getArg0() { return this + arg0; }
  Instruction* getArg1() { return this + arg1; }
  const Instruction* getKey() const { return this + key; }
  const Instruction* getArg0() const { return this + arg0; }
  const Instruction* getArg1() const { return this + arg1; }
  const Instruction* updateKey() {
    if (key) key = this[key].updateKey() - this;
    return getKey();
  }
  const Instruction* updateKey(const Instruction* newKey) {
    key = (key ? this[key].updateKey(newKey) : newKey) - this;
    return getKey();
  }

  void print(const Instruction* base);

  Opcode opcode;
  int key;
  int arg0; // also literal
  int arg1; // jump target
  int order;
};

extern Instruction countedMarker;

struct Block {
  //bool dominates(const Block& block) const {
  //  return dominatorID <= block.dominatorID && block.dominatorID < dominatorID + dominatorSize;
  //}
  //bool postDominates(const Block& block) const {
  //  return postDominatorID <= block.postDominatorID && block.postDominatorID < postDominatorID + postDominatorSize;
  //}

  Block* dominator;
  Block* head;
  //int dominatorID;
  //int dominatorSize;
  //Block* postDominator;
  //int postDominatorID;
  //int postDominatorSize;
  //size_t local_size;
  //size_t total_size;



  //Block* parent;
  //Instruction* instrs;
  size_t firstInstr;;
  size_t numInstrs;
  //size_t numPhis;
  //size_t numEchos;
  //Block** Preds;
  //Block** Succs;
  //size_t numPreds;
  //size_t numSuccs;
};

//struct Procedure {
//  Block* getFirstBlock() const {
//    return (Block*)((char*)this + firstBlockOffset);
//  }
//  Block* getLastBlock() const {
//    return (Block*)((char*)this + lastBlockOffset);
//  }
//  Procedure& setFirstBlock(Block* instr) {
//    firstBlockOffset = (int)((char*)instr - (char*)this);
//    return *this;
//  }
//  Procedure& setLastBlock(Block* instr) {
//    lastBlockOffset = (int)((char*)instr - (char*)this);
//    return *this;
//  }
//
//  int firstBlockOffset;
//  int lastBlockOffset;
//};

namespace Opcodes {
  extern OpcodeEx header;
  extern OpcodeEx headerDominates;
  extern OpcodeEx nop;
  extern OpcodeEx jump;
  extern OpcodeEx branch;
  extern OpcodeEx intValue;
  extern OpcodeEx ret;
  extern OpcodeEx copy;
  extern OpcodeEx phi;
  extern OpcodeEx add;
  extern OpcodeEx mul;
  extern OpcodeEx cmpeq;
  extern OpcodeEx cmplt;
  extern OpcodeEx cmple;
};

void print(Instruction* instrs, size_t numInstrs);

#if 0
union Event;
struct Object {
  enum Kind {
    HOLE,
    WALK_BACK,
    SKIP_BACK,
    USE,
    JUMP,
    BRANCH,
    INT_LITERAL,
    PHI,
    NOP,
    ADD,
    MUL,
    CMP_EQ,
    CMP_LT,
    CMP_LE,
  };

  bool isValue() const { return kind >= INT_LITERAL && kind <= CMP_LE; }
  Object& initObject(Kind kind) { this->kind = kind; return *this; }
  Object& initHole() { return initObject(HOLE); }
  const Event& asEvent() const { return *(const Event*)this; }
  Event& asEvent() { return *(Event*)this; }
  void print(int index);
  void emit(Instr* buffer);
  Kind kind;
};

struct Link : Object {
  Link& initWalkBack(int offsetToTarget) { initObject(WALK_BACK); this->offsetToTarget = offsetToTarget; return *this; }
  Link& initSkipBack(int offsetToTarget) { initObject(SKIP_BACK); this->offsetToTarget = offsetToTarget; return *this; }
  void print(int index);
  int offsetToTarget;
};

struct Use : Object {
  Use& init(int offsetToValue) { initObject(USE); this->offsetToValue = offsetToValue; return *this; }
  void print(int index);
  int offsetToValue; // value
};

struct Jump : Object {
  Jump& initJump(int jumpTarget) { initObject(JUMP); this->jumpTarget = jumpTarget; return *this; }
  Jump& initJumpcc(int jumpTarget) { initObject(BRANCH); this->jumpTarget = jumpTarget; return *this; }
  void print(int index);
  void emit(X64Builder& builder);
  int jumpTarget;
};

struct Value : Object {
  Value& initValue(Kind kind) { initObject(kind); invalidRegs = 0; pressure = 0; reg = 0; offsetToRep = 0; return *this; }
  void updateRep(int index, int rep);
  void print(int index);
  int pressure; // reuse me?
  unsigned invalidRegs;
  unsigned reg;
  int offsetToRep;
};

struct IntLiteral : Value {
  IntLiteral& init(int value) { initValue(INT_LITERAL); this->value = value; return *this; }
  void print(int index);
  void emit(X64Builder& builder);
  int value;
};

struct Instruction : Value {
  Instruction& initPhi() { initValue(PHI); return *this; }
  Instruction& initAdd() { initValue(ADD); return *this; }
  Instruction& initMul() { initValue(MUL); return *this; }
  Instruction& initEq() { initValue(CMP_EQ); return *this; }
  Instruction& initLt() { initValue(CMP_LT); return *this; }
  Instruction& initLe() { initValue(CMP_LE); return *this; }
  void print(int index);
  void emit(X64Builder& builder);
};

union Event {
  Event(Object object) { this->object = object; }
  Event(Value value) { this->value = value; }
  Event(Link link) { this->link = link; }
  Event(Use use) { this->use = use; }
  Event(Jump jump) { this->jump = jump; }
  Event(IntLiteral intLiteral) { this->intLiteral = intLiteral; }
  Event(Instruction instruction) { this->instruction = instruction; }
  void print(int index);
  Object::Kind kind;
  Object object;
  Link link;
  Use use;
  Jump jump;
  Value value;
  IntLiteral intLiteral;
  Instruction instruction;
};

void print(Event* events, size_t numEvents);
#endif
} // namespace Jagger