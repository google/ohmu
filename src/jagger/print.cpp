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

#include "types.h"
#include <stdio.h>

namespace Jagger {
//void Object::print(int index) {
//  printf("HOLE");
//}
//
//void Link::print(int index) {
//  switch (kind) {
//  case WALK_BACK: printf("walk to %d", index + offsetToTarget); break;
//  case SKIP_BACK: printf("skip to %d", index + offsetToTarget); break;
//  }
//}
//
//void Use::print(int index) {
//  printf("%d -> %d", index + offsetToValue, index);
//}
//
//void Value::print(int index) {
//  printf("{%04x : %04x} [%3d]", ~invalidRegs, reg, pressure);
//  if (offsetToRep)
//    printf(" %3d->%3d", index, index + offsetToRep);
//}
//
//void IntLiteral::print(int index) {
//  printf("%-6d ", value);
//  ((Value*)this)->print(index);
//  printf(" : int");
//}
//
//void Jump::print(int index) {
//  switch (kind) {
//  case JUMP: printf("jump to %d\n", index + jumpTarget); break;
//  case BRANCH: printf("branch to %d\n", index + jumpTarget); break;
//  }
//}

void Instruction::print(int index) {
  printf("%3d ", index);
  switch (opcode) {
  case NOP        : printf("NOP        "); break;
  case RET        : printf("RET        "); break;
  case JUMP       : printf("JUMP       "); break;
  case BRANCH     : printf("BRANCH     "); break;
  case INT_LITERAL: printf("INT_LITERAL"); break;
  case PHI        : printf("PHI        "); break;
  case ECHO       : printf("ECHO       "); break;
  case COPY       : printf("COPY       "); break;
  case ADD        : printf("ADD        "); break;
  case MUL        : printf("MUL        "); break;
  case CMP_EQ     : printf("CMP_EQ     "); break;
  case CMP_LT     : printf("CMP_LT     "); break;
  case CMP_LE     : printf("CMP_LE     "); break;
  }
  switch (opcode) {
  case NOP:
  case RET:
  case JUMP: printf(" jump to %d", index + arg0); break;
  case BRANCH: printf(" branch to %d (%d)", index + arg1, arg0); break;
  case INT_LITERAL: break;
  case PHI: printf(" (%d %d)", arg0, arg1); break;
  case ECHO:
  case COPY: printf(" (%d)", arg0); break;
  case ADD:
  case MUL:
  case CMP_EQ:
  case CMP_LT:
  case CMP_LE: printf(" (%d %d)", arg0, arg1); break;
  }
  switch (opcode) {
  case NOP:
  case RET:
  case JUMP:
  case BRANCH: break;
  case INT_LITERAL:
  case PHI:
  case ECHO:
  case COPY:
  case ADD:
  case MUL:
  case CMP_EQ:
  case CMP_LT:
  case CMP_LE: printf(" (%3d) {%04x (%0x4) : %04x} [%3d]", index + key, ~invalidRegs, preferredRegs, reg, pressure); break;
  }
  if (opcode == BRANCH) printf(" %-6d : int", arg0);
  printf("\n");
}

void print(Instruction* instrs, size_t numInstrs) {
  int index = 0;
  for (auto i = instrs, e = instrs + numInstrs; i != e; ++i)
    i->print(index++);
}
}