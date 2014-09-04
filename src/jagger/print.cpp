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
void Instruction::print(int index) {
  const Opcode* opcode = opcode;
  printf("%3d %-10s", index, opcode->name);
  if (opcode->isJump) printf(" jump to %d", index + arg1);
  if (opcode->hasResult)
    printf(" |%3d| {%04x (%04x) : %04x} [%2d]", index + key, ~invalidRegs, preferredRegs, reg, pressure);
  if (opcode->hasArg0) {
    printf(" (%d", index + arg0);
    if (opcode->hasArg1)
      printf(", %d", index + arg1);
    printf(")");
  }
  if (opcode->isIntLiteral)
    printf(" %-3d ", arg0);
  printf("\n");
}

void print(Instruction* instrs, size_t numInstrs) {
  int index = 0;
  for (auto i = instrs, e = instrs + numInstrs; i != e; ++i)
    i->print(index++);
}
}