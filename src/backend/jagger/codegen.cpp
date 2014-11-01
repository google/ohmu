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

// Peephole Notes:
// load->immediate
// mul->add/lea/shift
// div->shift
// cmp 0 folding
// address mode folding
// folding loads/stores into instrs
// 0-offset jump/branch elimination
// Commute arguments for destructive operations

#if 0
#include "types.h"
#include "interface.h"

namespace Jagger {
static const GP32Reg regs[] = {
  EAX, ECX, EDX, EBX,
  R8D, R9D, R10D, R11D,
  R12D, R13D, R14D, R15D,
};

static const char* const regNames[] = {
  "EAX", "ECX", "EDX", "EBX",
  "R8D", "R9D", "R10D", "R11D",
  "R12D", "R13D", "R14D", "R15D",
};

extern "C" unsigned char _BitScanReverse(unsigned long * Index, unsigned long Mask);

static int setToIndex(unsigned x) {
  unsigned long a;
  _BitScanReverse(&a, (unsigned long)x);
  return (int)a;
}

void Jump::emit(X64Builder& builder) {
#if 0
  if (kind == JUMP) {
    printf("JMP ???\n");
    builder.JMP(0);
  }
  else {
    printf("JMPcc ???\n");
    builder.JA(0);
  }
#endif
}

void IntLiteral::emit(X64Builder& builder) {
#if 0
  for (unsigned set = copySet; set; set &= ~(set & -set))
    printf("MOV %s, %d\n", regNames[setToIndex(set)], value);
#endif
}

void Instruction::emit(X64Builder& builder) {
#if 0
  switch (kind) {
  case Object::ADD: {
    unsigned reg0 = ((Event *)this)[-2].liveRange.reg;
    unsigned reg1 = ((Event *)this)[-1].liveRange.reg;
    printf("ADD %s, %s\n", regNames[setToIndex(reg0)],
           regNames[setToIndex(reg1)]);
    for (unsigned set = copySet & ~reg0; set; set &= ~(set & -set))
      printf("MOV %s, %s\n", regNames[setToIndex(set & -set)],
             regNames[setToIndex(reg0)]);
  } break;
  case Object::MUL:
    printf("MUL\n");
    break;
  case Object::CMP_EQ:
  case Object::CMP_LT:
  case Object::CMP_LE:
    printf("CMP\n");
    break;
    //i->instruction.emit(builder);
    break;
  }
#endif
}

void emitASM(X64Builder& builder, Event* events, size_t numEvents) {
#if 0
  for (auto i = events, e = events + numEvents; i != e; ++i)
    switch (i->kind) {
    case Object::JUMP:
    case Object::BRANCH:
      i->jump.emit(builder);
      break;
    case Object::INT_LITERAL:
      i->intLiteral.emit(builder);
      break;
    case Object::ADD:
    case Object::MUL:
    case Object::CMP_EQ:
    case Object::CMP_LT:
    case Object::CMP_LE:
      i->instruction.emit(builder);
      break;
  }
#endif
}
}
#endif
