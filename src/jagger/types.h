//===- event.cpp -----------------------------------------------*- C++ --*-===//
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

struct Block {
  int id;
  int numDominatedBlocks;
  Instruction* firstInstr;
  Instruction* lastInstr;
  Block* dominator;
  Block* postDominator;

  bool dominates(const Block& block) {
    return block.id < id + numDominatedBlocks && block.id > id;
  }
};

struct Instruction {
  struct Options {
    unsigned args;
  };

  enum OpCode {
    NOP,
    RET,
    JUMP,
    BRANCH,
    INT_LITERAL,
    PHI,
    ECHO, // could be NOP?
    COPY,
    ADD,
    MUL,
    CMP_EQ,
    CMP_LT,
    CMP_LE,
  } opcode;
  Block* block;
  int key;
  int arg0; // also int literal
  int arg1; // jump target
  unsigned invalidRegs;
  unsigned preferredRegs;
  unsigned reg;
  int pressure;

  Instruction& init(OpCode opcode, int arg0 = 0, int arg1 = 0) {
    this->opcode = opcode;
    this->arg0 = arg0;
    this->arg1 = arg1;
    invalidRegs = preferredRegs = reg = pressure = 0;
    return *this;
  }
  Instruction& init(int value) {
    return init(INT_LITERAL, value);
  }

  void print(int index);
};


void print(Instruction* blocks);

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