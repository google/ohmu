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
  const char name[16];
  bool hasResult;
  bool hasArg0;
  bool hasArg1;
  bool isJump;
  bool isIntLiteral;
  unsigned reg;
  unsigned arg0reg;
  unsigned arg1reg;
};

struct Instruction {
  Instruction() {}
  Instruction(const Block* block, const Opcode* opcode,
              const Instruction* arg0 = nullptr,
              const Instruction* arg1 = nullptr)
      : block(block),
        opcode(opcode),
        key(0),
        arg0(arg0),
        arg1(arg1),
        invalidRegs(0),
        preferredRegs(0),
        reg(0),
        pressure(0) {}

  void print(int index);

  const Block* block;
  const Opcode* opcode;
  const Instruction* key;
  const Instruction* arg0; // also literal
  const Instruction* arg1; // jump target
  unsigned invalidRegs;
  unsigned preferredRegs;
  unsigned reg;
  int pressure;
};

extern Instruction countedMarker;

struct Block {
  Block* parent;
  Instruction* instrs;
  size_t numInstrs;
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

struct Opcodes {
  Opcode nop;
  Opcode jump;
  Opcode branch;
  Opcode intValue;
  Opcode ret;
  Opcode echo;
  Opcode copy;
  Opcode phi;
  Opcode add;
  Opcode mul;
  Opcode cmpeq;
  Opcode cmplt;
  Opcode cmple;
};

extern const Opcodes globalOpcodes;

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