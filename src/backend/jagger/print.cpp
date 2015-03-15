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

namespace jagger {
namespace wax {
void Type::print() const {
  int numBits = 1 << (3 + size());
  switch (kind()) {
  case Type::BINARY: printf("bin%d", numBits); break;
  case Type::UNSIGNED: printf("uint%d", numBits); break;
  case Type::INTEGER: printf("int%d", numBits); break;
  case Type::FLOAT: printf("float%d", numBits); break;
  case Type::VOID: printf("void"); break;
  case Type::BOOLEAN: printf("bool"); break;
  case Type::ADDRESS: printf("addr"); break;
  case Type::STACK: printf("stack"); break;
  }
  if (count()) printf("[%d]", (count() >> 5) + 1);
}

void print(TypedRef instr) {
  switch (instr.type()) {
    case INVALID: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case NOP: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BLOCK_HEADER: {
      printf("%16s : %d", "BLOCK_HEADER", instr.as<BlockHeader>().payload());
    } break;
    case DATA_HEADER: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BYTES: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case ZERO: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case UNDEFINED_VALUE: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case STATIC_ADDRESS: {
      auto& payload = instr.as<StaticAddress>().payload();
      printf("%16s : %d", "STATIC_ADDRESS", payload.index());
      if (payload.flags() & Label::EXTERNAL) printf(" extern");
      if (payload.flags() & Label::THREAD_LOCAL) printf(" tls");
      if (payload.flags() & Label::CODE) printf(" x");
      if (payload.flags() & Label::WRITABLE) printf(" w");
      if (payload.flags() & Label::UNINITIALIZED) printf(" 0");
    } break;
    case USE: {
      printf("%16s : %d", "USE", instr.as<Use>().payload());
    } break;
    case PHI: {
      printf("%16s : %d", "PHI", instr.as<Use>().payload());
    } break;
    case PHI_ARGUMENT: {
      printf("%16s : %d", "PHI_ARGUMENT", instr.as<Use>().payload());
    } break;
    case CALL: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case CALL_SPMD: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case RETURN: {
      printf("%16s", "RET");
    } break;
    case INDIRECT_JUMP: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case JUMP: {
      printf("%16s", "JUMP");
    } break;
    case BRANCH: {
      printf("%16s", "BRANCH");
    } break;
    case SWITCH: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case COMPUTE_ADDRESS: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case PREFETCH: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case LOAD: {
      printf("%16s", "LOAD");
      auto& payload = instr.as<Load>().payload();
      if (payload.flags & LoadStorePayload::NON_TEMPORAL) printf(" nt");
      if (payload.flags & LoadStorePayload::UNALIGNED) printf(" u");
      printf(" : ");
      payload.type.print();
    } break;
    case STORE: {
      printf("%16s", "STORE");
      auto& payload = instr.as<Store>().payload();
      if (payload.flags & LoadStorePayload::NON_TEMPORAL) printf(" nt");
      if (payload.flags & LoadStorePayload::UNALIGNED) printf(" u");
      printf(" : ");
      payload.type.print();
    } break;
    case MEM_SET: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case MEM_COPY: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case EXTRACT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case INSERT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BROADCAST: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case PERMUTE: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case SHUFFLE: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BIT_TEST: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case NOT: {
      printf("%16s : ", "NOT");
      instr.as<Not>().payload().type.print();
    } break;
    case LOGIC: {
      auto& payload = instr.as<Logic>().payload();
      switch (payload.kind) {
      case LogicPayload::FALSE:
        printf("%16s", "FALSE <error>");
        break;
      case LogicPayload::NOR:
        printf("%16s", "NOR");
        break;
      case LogicPayload::GT:
        printf("%16s", "GT");
        break;
      case LogicPayload::NOTB:
        printf("%16s", "NOTB <error>");
        break;
      case LogicPayload::LT:
        printf("%16s", "LT");
        break;
      case LogicPayload::NOTA:
        printf("%16s", "NOTA <error>");
        break;
      case LogicPayload::XOR:
        printf("%16s", "XOR");
        break;
      case LogicPayload::NAND:
        printf("%16s", "NAND");
        break;
      case LogicPayload::AND:
        printf("%16s", "AND");
        break;
      case LogicPayload::EQ:
        printf("%16s", "EQ");
        break;
      case LogicPayload::A:
        printf("%16s", "A <error>");
        break;
      case LogicPayload::GE:
        printf("%16s", "GE");
        break;
      case LogicPayload::B:
        printf("%16s", "B <error>");
        break;
      case LogicPayload::LE:
        printf("%16s", "LE");
        break;
      case LogicPayload::OR:
        printf("%16s", "OR");
        break;
      case LogicPayload::TRUE:
        printf("%16s", "TRUE <error>");
        break;
      }
      printf(" : ");
      payload.type.print();
    } break;
    case LOGIC3: {
      auto& payload = instr.as<Logic3>().payload();
      printf("%16s : %02x : ", "LOGIC3", payload.kind);
      payload.type.print();
    } break;
    case SHIFT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BITFIELD_EXTRACT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BITFIELD_INSERT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case BITFIELD_CLEAR: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case COUNT_ZEROS: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case POPCNT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case COMPARE: {
      auto& payload = instr.as<Compare>().payload();
      switch (payload.kind) {
        case ComparePayload::FALSE:
          printf("%16s", "FALSE");
          break;
        case ComparePayload::LT:
          printf("%16s", "LT");
          break;
        case ComparePayload::EQ:
          printf("%16s", "EQ");
          break;
        case ComparePayload::LE:
          printf("%16s", "LE");
          break;
        case ComparePayload::GT:
          printf("%16s", "GT");
          break;
        case ComparePayload::NEQ:
          printf("%16s", "NEQ");
          break;
        case ComparePayload::GE:
          printf("%16s", "GE");
          break;
        case ComparePayload::ORD:
          printf("%16s", "ORD");
          break;
        case ComparePayload::UNORD:
          printf("%16s", "UNORD");
          break;
        case ComparePayload::LTU:
          printf("%16s", "LTU");
          break;
        case ComparePayload::EQU:
          printf("%16s", "EQU");
          break;
        case ComparePayload::LEU:
          printf("%16s", "LEU");
          break;
        case ComparePayload::GTU:
          printf("%16s", "GTU");
          break;
        case ComparePayload::NEQU:
          printf("%16s", "NEQU");
          break;
        case ComparePayload::GEU:
          printf("%16s", "GEU");
          break;
        case ComparePayload::TRUE:
          printf("%16s", "TRUE");
          break;
      }
      printf(" : ");
      payload.type.print();
    } break;
    case MIN: {
      printf("%16s : ", "MIN");
      instr.as<Min>().payload().type.print();
    } break;
    case MAX: {
      printf("%16s : ", "MAX");
      instr.as<Max>().payload().type.print();
    } break;
    case NEG: {
      printf("%16s : ", "NEG");
      instr.as<Neg>().payload().type.print();
    } break;
    case ABS: {
      printf("%16s : ", "ABS");
      instr.as<Abs>().payload().type.print();
    } break;
    case ADD: {
      printf("%16s : ", "ADD");
      instr.as<Add>().payload().type.print();
    } break;
    case SUB: {
      printf("%16s : ", "SUB");
      instr.as<Sub>().payload().type.print();
    } break;
    case MUL: {
      printf("%16s : ", "MUL");
      instr.as<Mul>().payload().type.print();
    } break;
    case DIV: {
      printf("%16s : ", "DIV");
      instr.as<Div>().payload().type.print();
    } break;
    case MULHI: {
      printf("%16s : ", "MULHI");
      instr.as<Mulhi>().payload().type.print();
    } break;
    case MOD: {
      printf("%16s : ", "MOD");
      instr.as<Mod>().payload().type.print();
    } break;
    case RCP: {
      printf("%16s : ", "RCP");
      instr.as<Rcp>().payload().type.print();
    } break;
    case SQRT: {
      printf("%16s : ", "SQRT");
      instr.as<Sqrt>().payload().type.print();
    } break;
    case RSQRT: {
      printf("%16s : ", "RSQRT");
      instr.as<Rsqrt>().payload().type.print();
    } break;
    case EXP2: {
      printf("%16s : ", "EXP2");
      instr.as<Exp2>().payload().type.print();
    } break;
    case ROUND: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case CONVERT: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case FIXUP: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case ATOMIC_XCHG: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case ATOMIC_COMPARE_XCHG: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case ATOMIC_LOGIC_XCHG: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
    case ATOMIC_ADD_XCHG: {
      printf("<%02x:%08x>", instr.type(), instr.data());
    } break;
  }
}
} // namespace wax
} // namespace jagger

#if 0
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
#endif
