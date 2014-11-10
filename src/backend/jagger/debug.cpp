//===- debug.cpp -----------------------------------------------*- C++ --*-===//
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

namespace {
const char opcodeNames[][20] = {
    "NOP",    "USE",     "MUTED_USE", "HEADER",    "HEADER_DOMINATES",
    "INT32",  "LOAD",    "STORE",     "ULOAD",     "USTORE",
    "GATHER", "SCATTER", "SEXT",      "ZEXT",      "FCVT",
    "AND",    "OR",      "ANDN",      "ORN",       "XOR",
    "XNOR",   "NAND",    "NOR",       "NOT",       "SLL",
    "SLR",    "SAR",     "ROL",       "ROR",       "MIN",
    "MAX",    "ADD",     "SUB",       "SUBR",      "ADDN",
    "ADC",    "SBB",     "NEG",       "ABS",       "MUL",
    "MULHI",  "DIV",     "MOD",       "RCP",       "AOS",
    "AOSOA",  "MADD",    "MSUB",      "MSUBR",     "MADDN",
    "FMADD",  "FMSUB",   "FMSUBR",    "FMADDN",    "EQ",
    "NEQ",    "LT",      "LE",        "ORD",       "EQU",
    "NEQU",   "LTU",     "LEU",       "UNORD",     "JUMP",
    "BRANCH", "CALL",    "RET",       "BT",        "BTS",
    "BTR",    "BTC",     "CTZ",       "CLZ",       "POPCNT",
    "SQRT",   "RSQRT",   "SHUFFLE",   "BROADCAST", "EXTRACT",
    "INSERT", "MEMSET",  "MEMCPY",
};

const char aliasSetNames[] = {"???", "GPR", "FLAGS", "MMX",
                              "SSE", "???", "???",   "???"};

void printDebug(Event event) {
  if (!event.isValue)
    printf("%s", opcodeNames[event.code]);
  else if (event.isFixed)
    printf("reg-fixed [%s:%d]", aliasSetNames[event.aliasSet], event.fixedReg);
  else
    printf("%s [%s]", event.isCopy ? event.isPhiKind ? "phi-copy" : "copy"
                                   : event.isPhiKind ? "phi" : "value",
           aliasSetNames[event.aliasSet]);
}

void printDebug(Event* events, size_t numEvents) {
  for (size_t i = 0; i < numEvents; ++i) {
    printf("%3d > ", i);
    printDebug(events[i]);
    printf(" %d\n", events[i].data);
  }
}
}  // namespace {
