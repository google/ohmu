//===- x64lowring.cpp ------------------------------------------*- C++ --*-===//
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

#if 0
void allocate(Core::EventList in, size_t numEvents) {
  computeOffsets();
  prefixSumOffsets();
  translate();
  createAndSortX64Ops();
  lowerKnownValues();
  walkUsesCountConflicts();
  allocateConflicts();
  walkUsesGatherConflicts();
  sortConflicts(); // TODO: do we need this?
  allocateCopies();
  listCopies(); // we hope to eliminate them
  sortCopies(); // TODO: do we need this?
  createWorklist(); // of registers
  sortWorklist();
  renumberAndResortConflicts(); // with work positions
  renumberAndResortCopies(); // with work positions
  allocateRegisters();
  countGeneratedInstrs();
  prefixSumGeneratedInstrs();
  allocateInstrs();
  generateInstrs();
}
#endif

#if 0

namespace Jagger {
namespace {

#if 0
  struct X64Opcode {
    // Vex uint 3.
    uint code_map : 2;
    uint evex : 1;
    //uint invalid : 1;
    //uint r1 : 1;
    //uint long_vex : 3;

    // The opcode/raw data info.
    uint opcode : 8;
    //uint align_pad : 4;
    //uint raw_data : 1;
  //uint: 3;

    // Postfixes flags.
    uint imm_size : 2;
    uint has_imm : 1;
    uint rip_addr : 1;
    uint has_modrm : 1;
    uint has_sib : 1;
    uint fixed_base : 1;
    uint force_disp : 1;

    // Prefixes flags.
    uint lock_rep : 2;
    uint size_prefix : 1;
    uint addr_prefix : 1;
    uint use_vex : 1;
    uint use_rex : 1;
    uint segment : 2;

    // Rex uint.
    //uint b : 1;
    //uint x : 1;
    //uint r : 1;
    uint w : 1;
  //uint: 2;
  //  uint rex_1 : 1;
  //uint: 1;

    // Vex uint 2.
    uint simd_prefix : 2;
    uint l : 1;
    //uint vvvv : 4;
    uint e : 1; // same as W

    // Modrm.
    //uint rm : 3;
    uint reg : 3;
    uint mod : 2;

    // Sib.
    //uint base : 3;
    //uint index : 3;
    uint scale : 2;

  };
#endif
enum X64GPR { RAX, RDX, RCX, RBX, RBP, RSP, RSI, RDI };
enum X64RegisterFile { GPR = 1, FLAGS, VPU, MASK, MMX };

struct X64MetaData {
  uchar used : 1;
  uchar usedOnce : 1;
};

struct X64RegisterBuilder : EventBuilder {
  inline size_t skip(size_t i, EventBuilder in, size_t j);
  inline size_t echo(size_t i, EventBuilder in, size_t j);
  inline size_t add(size_t i, EventBuilder in, size_t j);
  inline size_t sub(size_t i, EventBuilder in, size_t j);
  inline size_t compare(size_t i, EventBuilder in, size_t j);
  inline size_t mul(size_t i, EventBuilder in, size_t j);
  inline size_t branch(size_t i, EventBuilder in, size_t j);

  size_t lower(size_t i, uchar code, uint data) {
    if (code == ADD &&
        TypeDesc(BasicData(data).type).type != FLOAT &&
        TypeDesc(BasicData(data).type).vectorWidth == VEC1) {
      }
  }
};

typedef size_t(X64RegisterBuilder::*LowerFN)(size_t, EventBuilder, size_t);

LowerFN scalarTable[NUM_OPCODES];

size_t X64RegisterBuilder::skip(size_t i, EventBuilder in, size_t j) {
  return i;
}

size_t X64RegisterBuilder::echo(size_t i, EventBuilder in, size_t j) {
  return op(i, in.code(j), in.data(j));
}

size_t X64RegisterBuilder::add(size_t i, EventBuilder in, size_t j) {
  i = op(i, in.code(j - 2), in.data(j - 2));
  i = op(i, in.code(j - 1), in.data(j - 1));
  i = op(i, DESTRUCTIVE_VALUE | GPR, 0);
  i = op(i, VALUE | FLAGS, 0);
  return i;
}

size_t X64RegisterBuilder::sub(size_t i, EventBuilder in, size_t j) {
  i = op(i, in.code(j - 2), in.data(j - 2));
  i = op(i, DESTRUCTIVE_VALUE | GPR, 0);
  i = op(i, in.code(j - 1), in.data(j - 1));
  i = op(i, VALUE | FLAGS, 0);
  return i;
}

size_t X64RegisterBuilder::mul(size_t i, EventBuilder in, size_t j) {
  i = op(i, in.code(j - 2), in.data(j - 2));
  i = op(i, in.code(j - 1), in.data(j - 1));
  i = op(i, CLOBBER_LIST, RDX);
  auto hiHint = i;
  i = op(i, REGISTER_HINT, 0);
  i = op(i, CLOBBER_LIST, RAX);
  auto loHint = i;
  i = op(i, REGISTER_HINT, 0);
  i = op(i, REGISTER_HINT, in.data(j - 2));
  i = op(i, REGISTER_HINT, in.data(j - 1));
  this->data(loHint) = i;
  i = op(i, VALUE | GPR, 0); // mulhi
  this->data(hiHint) = i;
  i = op(i, VALUE | GPR, 0); // mullo
  i = op(i, VALUE | FLAGS, 0);
  return i;
}

size_t X64RegisterBuilder::compare(size_t i, EventBuilder in, size_t j) {
  i = op(i, in.code(j - 2), in.data(j - 2));
  i = op(i, in.code(j - 1), in.data(j - 1));
  i = op(i, VALUE | FLAGS, 0);
  return i;
}

size_t X64RegisterBuilder::branch(size_t i, EventBuilder in, size_t j) {
  return op(i, USE, in.data(j - 1));
}

#if 0
EventList lower(const EventList& in) {
  if (!in.numEvents) return EventList();
  auto sizes = new uint[in.numEvents] - in.first;
  for (auto i = in.first, e = in.bound; i != e; ++i) {
    sizes[i] = lower(in.builder, i);
  }
  delete[] (sizes + in.first);
}
#endif

}  // namespace
}  // namespace Jagger
#endif
