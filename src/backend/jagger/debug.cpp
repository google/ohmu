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

#include "til/TIL.h"
#include "til/Global.h"
#include "til/VisitCFG.h"
#include "types.h"
#include <stdio.h>

bool validateTIL(ohmu::til::BasicBlock* block) {
  if (!block) {
    printf("block is null.\n");
    return false;
  }
  if (block->parent()->blockID() >= block->blockID()) {
    printf("parent block has id greater than current block.\n");
    return false;
  }
  if (block->arguments().size() != 0) {
    if (block->predecessors().size() == 0) {
      printf("block has arguments but no predecessors.\n");
      return false;
    }
    for (auto& pred : block->predecessors()) {
      // todo: validate that pred has been validated
      if (pred->terminator()->opcode() != ohmu::til::COP_Goto) {
        printf("block with arguments has predecessor"
               " that doesn't terminate with a goto.\n");
        return false;
      }
    }
    if (!block->terminator()) {
      printf("block doesn't have a terminator.\n");
      return false;
    }
  }
  return true;
}

bool validateTIL(ohmu::til::SCFG* cfg) {
  if (!cfg) {
    printf("cfg is null.\n");
    return false;
  }
  if (cfg->blocks().size() == 0) {
    printf("cfg contains no blocks.\n");
    return false;
  }
  for (auto& block : cfg->blocks()) {
    if (!validateTIL(block.get()))
      return false;
    if (block->cfg() != cfg) {
      printf("block doesn't point back to cfg.\n");
      return false;
    }
  }
  return true;
}

bool validateTIL(ohmu::til::Global* global) {
  if (!global) {
    printf("module is null.\n");
    return false;
  }
  ohmu::til::VisitCFG visitCFG;
  visitCFG.traverseAll(global->global());
  auto& cfgs = visitCFG.cfgs();
  if (cfgs.empty()) {
    printf("module has no cfgs.");
    return false;
  }
  for (auto cfg : cfgs)
    if (!validateTIL(cfg))
      return false;
  return true;
}

namespace {
const char* opcodeNames[] = {
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

const char* aliasSetNames[] = {"???", "GPR", "FLAGS", "MMX",
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
}  // namespace {

void printDebug(Event* events, size_t numEvents) {
  for (size_t i = 0; i < numEvents; ++i) {
    printf("%3d : %5d > ", i, events[i].data);
    printDebug(events[i]);
    printf("\n");
  }
}
