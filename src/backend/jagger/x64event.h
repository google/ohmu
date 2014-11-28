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

#include "types.h"

namespace Jagger {

enum X64Opcode {
  AND, OR, XOR, ADD,
  SUB,
  NOT, NEG,
  TEST, CMP,
  SLL, SLR, SAR, ROL, ROR,
  MUL, DIV, IMUL, IDIV,
  LEA,
  JMP, RET,
  JZ, JNZ,
  IMM32, LOAD_IMM32,
};

enum X64RegisterFile {
  GPR = 1,
  FLAGS,
  VPU,
  MASK,
  MMX,
};

enum X64GPR {
  RAX, RDX, RCX, RBX, RBP, RSP, RSI, RDI
};

struct X64EventBuilder : EventBuilder {
  inline size_t result(size_t i, X64RegisterFile file);
  inline size_t destructiveResult(size_t i, X64RegisterFile file);
  inline size_t add(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t sub(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t mul(size_t i, uint arg0, uint arg1, Type type, LogBits logBits);
  inline size_t cmp(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t test(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t jmp(size_t i, uint target);
  inline size_t jz(size_t i, uint arg0, uint target);
  inline size_t ret(size_t i);
  inline size_t imm32(size_t i, uint value);
};

size_t X64EventBuilder::result(size_t i, X64RegisterFile file) {
  return op(i, VALUE | file, 0);
}

size_t X64EventBuilder::destructiveResult(size_t i, X64RegisterFile file) {
  return op(i, DESTRUCTIVE_VALUE | file, 0);
}

size_t X64EventBuilder::add(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, X64Opcode::ADD, logBits);
  i = use(i, arg1);
  i = use(i, arg0);
  i = destructiveResult(i, GPR);
  i = result(i, FLAGS);
  return i;
}

size_t X64EventBuilder::sub(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, X64Opcode::SUB, logBits);
  i = use(i, arg0);
  i = destructiveResult(i, GPR);
  i = use(i, arg1);
  i = result(i, FLAGS);
  return i;
}

size_t X64EventBuilder::mul(size_t i, uint arg0, uint arg1, Type type, LogBits logBits) {
  i = op(i, X64Opcode::MUL, (type << 3) + logBits);
  i = use(i, arg0);
  i = use(i, arg1);
  i = op(i, CLOBBER_LIST, RDX);
  auto hiHint = i;
  i = hint(i, 0);
  i = op(i, CLOBBER_LIST, RAX);
  auto loHint = i;
  i = hint(i, 0);
  i = hint(i, arg0);
  i = hint(i, arg1);
  data(loHint) = i;
  i = result(i, GPR);
  data(hiHint) = i;
  i = result(i, GPR);
  i = result(i, FLAGS);
  return i;
}

size_t X64EventBuilder::cmp(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, CMP, logBits);
  i = use(i, arg0);
  i = use(i, arg1);
  i = result(i, FLAGS);
  return i;
}

size_t X64EventBuilder::test(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, TEST, logBits);
  i = use(i, arg0);
  i = use(i, arg1);
  i = result(i, FLAGS);
  return i;
}

size_t X64EventBuilder::jmp(size_t i, uint target) {
  return op(i, JMP, target);
}

size_t X64EventBuilder::jz(size_t i, uint arg0, uint target) {
  i = use(i, arg0);
  i = op(i, JZ, target);
  return i;
}

size_t X64EventBuilder::ret(size_t i) {
  return op(i, RET, 0);
}

size_t X64EventBuilder::imm32(size_t i, uint value) {
  i = op(i, IMM32, value);
  i = op(i, NOP, 0); //< in case it gets upgraded to a load
  return i;
}

} //  namespace Jagger
