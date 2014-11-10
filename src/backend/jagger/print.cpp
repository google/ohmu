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
#include <map>
#include "x64builder\x64builder.h"

static char OpcodeNames[][20] = {
  "NOP", "USE", "MUTED_USE", "HEADER", "HEADER_DOMINATES",
  "INT32", "LOAD", "STORE", "ULOAD", "USTORE", "GATHER", "SCATTER",
  "SEXT", "ZEXT", "FCVT",
  "AND", "OR", "ANDN", "ORN", "XOR", "XNOR", "NAND", "NOR", "NOT",
  "SLL", "SLR", "SAR", "ROL", "ROR",
  "MIN", "MAX",
  "ADD", "SUB", "SUBR", "ADDN", "ADC", "SBB", "NEG", "ABS",
  "MUL", "MULHI", "DIV", "MOD", "RCP",
  "AOS", "AOSOA",
  "MADD", "MSUB", "MSUBR", "MADDN",
  "FMADD", "FMSUB", "FMSUBR", "FMADDN",
  "EQ", "NEQ", "LT", "LE", "ORD", "EQU", "NEQU", "LTU", "LEU", "UNORD",
  "JUMP", "BRANCH", "CALL", "RET",
  "BT", "BTS", "BTR", "BTC",
  "CTZ", "CLZ", "POPCNT",
  "SQRT", "RSQRT",
  "SHUFFLE", "BROADCAST", "EXTRACT", "INSERT",
  "MEMSET", "MEMCPY",
};

void print_stream(EventStream events, size_t numInstrs) {
  for (size_t i = 0; i < numInstrs; ++i) {
    auto code = events[i].code;
    printf("%3d > ", i);
    if (code <= MEMCPY)
      printf("%s", OpcodeNames[code]);
    else {
      if ((code & VALUE_MASK) == VALUE) printf("VALUE");
      if ((code & VALUE_MASK) == PHI) printf("PHI");
      if ((code & VALUE_MASK) == COPY) printf("COPY");
      if ((code & VALUE_MASK) == PHI_COPY) printf("PHI_COPY");
    }
    printf(" : %d\n", events[i].data);
  }
}

void print_asm(EventStream events, size_t numEvents) {
  for (size_t i = 0; i < numEvents; ++i) {
    auto code = events[i].code;
    if (code & VALUE) {
      if (!(code & 0x20)) continue;  //< is not a copy
      auto dst = events[i].data;
      if ((code & VALUE_MASK) == PHI_COPY) dst = events[dst].data;
      auto src = events[events[i - 1].data].data;
      if (src != dst) printf("copy %02x %02x\n", dst, src);
      continue;
    }
    if (code <= HEADER_DOMINATES)
      continue;
    switch (code) {
    case INT32:
      printf("mov %02x '%d'\n", events[i - 1].data, events[i].data);
      break;
    case ADD: {
      auto dst = events[i - 2].data;
      if (events[i - 2].code == MUTED_USE)
        dst = events[dst].data;
      auto src = events[events[i - 4].data].data;
      printf("add %02x %02x\n", dst, src);
      break;
    }
    case SUB:
      printf("sub %02x %02x\n", events[events[i - 3].data].data,
        events[events[i - 2].data].data);
      break;
    case MUL: {
      auto dst = events[events[i - 9].data].data;
      auto src = events[events[i - 8].data].data;
      if (src == 1 || dst == 2) std::swap(src, dst);
      if (dst != 1) printf("copy 01 %02x\n", dst);
      if (src != 2) printf("copy 02 %02x\n", src);
      printf("mul 01 02\n");
      if (events[i - 2].data != 1) printf("copy %02x 01\n", events[i - 2].data);
      break;
    }
    case EQ:
      printf("cmp %02x %02x\n", events[events[i - 3].data].data,
             events[events[i - 2].data].data);
      break;
    case JUMP:
      printf("JUMP %d\n", events[i].data);
      break;
    case BRANCH:
      printf("JE %d\n", events[i].data);
      break;
    case RET:
      if (events[events[i - 2].data].data != 1)
        printf("copy 01 %0x2", events[events[i - 2].data].data);
      break;
    default: printf("unknown op! %02x\n", code);
    }
  }
}

GP32Reg reg(Data value) {
  assert(value == (value & -value));
  unsigned ret;
  _BitScanForward((unsigned long*)&ret, value);
  return (GP32Reg)ret;
}

void make_asm(EventStream events, size_t numEvents) {
  X64Builder builder;
  for (size_t i = 0; i < numEvents; ++i) {
    auto code = events[i].code;
    if (code & VALUE) {
      if (!(code & 0x20)) continue;  //< is not a copy
      auto dst = events[i].data;
      if ((code & VALUE_MASK) == PHI_COPY) dst = events[dst].data;
      auto src = events[events[i - 1].data].data;
      if (src != dst) builder.MOV(reg(dst), reg(src));
      continue;
    }
    if (code < HEADER)
      continue;
    if (code <= HEADER_DOMINATES)
      builder.Label();
    switch (code) {
    case INT32:
      builder.MOV(reg(events[i - 1].data), (int)events[i].data);
      break;
    case ADD: {
      auto dst = events[i - 2].data;
      if (events[i - 2].code == MUTED_USE)
        dst = events[dst].data;
      auto src = events[events[i - 4].data].data;
      builder.ADD(reg(dst), reg(src));
      break;
    }
    case SUB:
      builder.ADD(reg(events[events[i - 3].data].data), reg(events[events[i - 2].data].data));
      break;
    case MUL: {
      auto dst = events[events[i - 9].data].data;
      auto src = events[events[i - 8].data].data;
      if (src == 1 || dst == 2) std::swap(src, dst);
      if (dst != 1) builder.MOV(EAX, reg(dst));
      if (src != 2) builder.MOV(EDX, reg(src));
      // TODO: check if EDX is required!
      builder.MUL(EAX, EDX);
      if (events[i - 2].data != 1) builder.MOV(reg(events[i - 1].data), EAX);
      break;
    }
    case EQ:
      builder.CMP(reg(events[events[i - 3].data].data),
                  reg(events[events[i - 2].data].data));
      break;
    case JUMP:
      builder.JMP((int)events[i].data - 1);
      break;
    case BRANCH: {
      auto& test = events[events[i - 1].data - 1];
      printf("------ %d\n", events[i - 1].data);
      if (test.data == 0) {
        printf(">>>>>>>>> %d!\n", test.code);
        switch (test.code) {
        case EQ:;
        }
      } else {
      }
      builder.JNZ((int)events[i].data - 1);
      break;
    }
    case RET:
      if (events[events[i - 2].data].data != 1)
        builder.MOV(EAX, reg(events[events[i - 2].data].data));
      builder.RET();
      break;
    default: printf("unknown op! %02x\n", code);
    }
  }
  unsigned char buffer[1024];
  auto tmp = builder.Encode(buffer);
  for (auto i = buffer; i < tmp; i++)
    printf("%02x  ", *i);
}
