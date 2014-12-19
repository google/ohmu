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

namespace X64 {
using Core::uchar;
using Core::uint;

enum X64Event {
  X64_CASE_HEADER, X64_JOIN_HEADER,
  REG, RM, VVVV, BASE, RIP, ZERO_BASE, INDEX_1, INDEX_2, INDEX_4, INDEX_8,
  DISP,
  IMM_8,
  IMM_16,
  IMM_32,
  IMM_64,
  IMM_64,
  X64_OPCODE,
  GPR_VALUE, VEC_VALUE, FLAGS_VALUE, MASK_VALUE, MMX_VALUE,
  KNOWN_GPR_VALUE, KNOWN_VEC_VALUE, KNOWN_FLAGS_VALUE, KNOWN_MASK_VALUE, KNOWN_MMX_VALUE,
};

enum Register { EAX, EDX, ECX, EBX };

union Instruction {
  struct {
    unsigned foramt : 2; // none/rex/vex/evex
    unsigned lockRep : 1;
    unsigned size : 1;
    unsigned address : 1;
    unsigned override : 3;

    unsigned rmResult : 1;
    unsigned regResult : 1;
    unsigned modrm_reg : 3;
    unsigned sib_scale : 2; // don't need
    unsigned vex_E : 1;

    unsigned vex_mm : 2;
    unsigned vex_L : 1;
    unsigned rex_W : 1;
    unsigned evex_b : 1;
    unsigned evex_L : 1;
    unsigned evex_L1 : 1;
    unsigned evex_z : 1;

    unsigned opcode : 8;
  };
  unsigned bits;
};

namespace OpcodeBits {
  const unsigned add = 0;
  const unsigned sub = 0;
  const unsigned mul = 0;
  const unsigned mov = 0;
}

struct X64Builder : Core::EventBuilder {
  inline size_t add(size_t i, EventBuilder in, size_t index);
  inline size_t sub(size_t i, EventBuilder in, size_t index);
  inline size_t mul(size_t i, EventBuilder in, size_t index);
#if 0
  inline size_t cmp(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t test(size_t i, uint arg0, uint arg1, LogBits logBits);
  inline size_t jmp(size_t i, uint target);
  inline size_t jz(size_t i, uint arg0, uint target);
  inline size_t ret(size_t i);
  inline size_t imm32(size_t i, uint value);
#endif
};

size_t X64Builder::add(size_t i, EventBuilder in, size_t index) {
  // TODO: fold load
  if (in.kind(index - 2) == Core::USE) {
    i = op(i, X64_OPCODE, OpcodeBits::mov);
    i = op(i, RM, in.data(index - 1));
    i = op(i, GPR_VALUE, 0);
  }
  i = op(i, X64_OPCODE, OpcodeBits::add); // TODO: switch on incoming number of bits
  i = op(i, REG, in.kind(index - 2) == Core::USE ? i - 2 : in.data(in.data(index - 2) - 1));
  i = op(i, RM, in.data(in.data(index - 1) - 1));
  i = op(i, FLAGS_VALUE, 0);
  return i;
}

size_t X64Builder::sub(size_t i, EventBuilder in, size_t index) {
  // TODO: fold load
  if (in.kind(index - 2) == Core::USE) {
    i = op(i, X64_OPCODE, OpcodeBits::mov);
    i = op(i, RM, in.data(index - 2));
    i = op(i, GPR_VALUE, 0);
  }
  i = op(i, X64_OPCODE, OpcodeBits::sub); // TODO: switch on incoming number of bits
  i = op(i, REG, in.kind(index - 2) == Core::USE ? i - 2 : in.data(in.data(index - 2) - 1));
  i = op(i, RM, in.data(in.data(index - 1) - 1));
  i = op(i, FLAGS_VALUE, 0);
  return i;
}

size_t X64Builder::mul(size_t i, EventBuilder in, size_t index) {
  i = op(i, X64_OPCODE, OpcodeBits::mov);
  i = op(i, RM, in.data(index - 2)); // set the value to hint-value
  i = op(i, KNOWN_GPR_VALUE, EAX);
  i = op(i, X64_OPCODE, OpcodeBits::mul); // TODO: switch on incoming number of bits and signed/unsigned
  i = op(i, RM, in.data(index - 1)); // set the value to hint-value
  i = op(i, KNOWN_GPR_VALUE, 0);
  i = op(i, KNOWN_GPR_VALUE, 1);
  i = op(i, FLAGS_VALUE, 0);
  i = op(i, X64_OPCODE, OpcodeBits::mov /*from eax*/); 
  i = op(i, GPR_VALUE, 0);
  // TODO: add a second mullo/mulhi combo translator
  //i = op(i, X64_OPCODE, OpcodeBits::mov /*from edx*/);
  //i = op(i, HINT_GPR_VALUE, EDX);
  return i;
}

#if 0
size_t X64Builder::cmp(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, CMP, logBits);
  i = use(i, arg0);
  i = use(i, arg1);
  i = result(i, FLAGS);
  return i;
}

size_t X64Builder::test(size_t i, uint arg0, uint arg1, LogBits logBits) {
  i = op(i, TEST, logBits);
  i = use(i, arg0);
  i = use(i, arg1);
  i = result(i, FLAGS);
  return i;
}

size_t X64Builder::jmp(size_t i, uint target) {
  return op(i, JMP, target);
}

size_t X64Builder::jz(size_t i, uint arg0, uint target) {
  i = use(i, arg0);
  i = op(i, JZ, target);
  return i;
}

size_t X64Builder::ret(size_t i) {
  return op(i, RET, 0);
}

size_t X64Builder::imm32(size_t i, uint value) {
  i = op(i, IMM32, value);
  i = op(i, NOP, 0); //< in case it gets upgraded to a load
  return i;
}
#endif

// sort values
// lower known values
// mark conflicts
// mark goals, found by the mov instruction ?
// allocate values

} //  namespace Jagger
