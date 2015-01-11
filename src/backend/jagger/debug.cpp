//===- debug.cpp -----------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License", "Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing", "software
// distributed under the License is distributed on an "AS IS" BASIS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND", "either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//

#include "til/TIL.h"
#include "til/Global.h"
#include "til/VisitCFG.h"
#include "types.h"
#include <stdarg.h>
#include <stdio.h>

namespace jagger {
void error(const char* format, ...) {
  va_list argList;
  va_start(argList, format);
  vprintf(format, argList);
  va_end(argList);
  exit(1);
}

//for (auto& block : blockArray) {
//  printf("[%d :", block.blockID);
//  for (auto& other : blockArray)
//    if (block.dominates(other))
//      printf(" %d", other.blockID);
//  printf(" :");
//  for (auto& other : blockArray)
//    if (block.postDominates(other))
//      printf(" %d", other.blockID);
//  printf("]\n");
//}

void print(const wax::Module& module) {
  auto neighbors = module.neighborArray.root;
  for (auto& fun : module.functionArray) {
    printf("function %d\n", &fun - module.functionArray.begin());
    for (auto& block : module.blockArray.slice(fun.blocks)) {
      printf(" block %d (%d)\n", &block - module.blockArray.begin(), block.blockID);
      printf("  caseIndex       = %d\n", block.caseIndex);
      printf("  predecessors    = {");
      if (block.predecessors.size()) {
        printf("%d", neighbors[block.predecessors.first]);
        for (auto i = block.predecessors.first + 1,
          e = block.predecessors.bound;
          i != e; ++i)
          printf(", %d", neighbors[i]);
      }
      printf("}\n");
      printf("  phiIndex        = %d\n", block.phiIndex);
      printf("  successors      = {");
      if (block.successors.size()) {
        printf("%d", neighbors[block.successors.first]);
        for (auto i = block.successors.first + 1, e = block.successors.bound;
          i != e; ++i)
          printf(", %d", neighbors[i]);
      }
      printf("}\n");
      printf("  loopDepth       = %d\n", block.loopDepth);
      printf("  dominator       = %d\n", block.dominator);
      printf("  dominates       = {");
      bool first = true;
      for (auto& other : module.blockArray.slice(fun.blocks))
        if (&block != &other && block.dominates(other)) {
          if (!first) printf(", ");
          first = false;
          printf("%d", &other - module.blockArray.begin());
        }
      printf("}\n");
      printf("  postDominator   = %d\n", block.postDominator);
      printf("  postDominates   = {");
      first = true;
      for (auto& other : module.blockArray.slice(fun.blocks))
        if (&block != &other && block.postDominates(other)) {
          if (!first) printf(", ");
          first = false;
          printf("%d", &other - module.blockArray.begin());
        }
      printf("}\n");
      printf("  trees           = %d, %d; %d, %d\n", block.domTreeID,
        block.domTreeSize, block.postDomTreeID, block.postDomTreeSize);
      printf("  events          = [%d, %d)\n", block.firstEvent, block.boundEvent);
      printf("\n");
    }
  }
}
}  // namespace jagger

#if 0
namespace Core {

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

const char* opcodeNames[] = {
    "NOP",             "CASE_HEADER",     "JOIN_HEADER",  "USE",
    "LAST_USE",        "ONLY_USE",        "ANCHOR",        "JOIN_COPY",
    "PHI",             "IMMEDIATE_BYTES", "BYTES_HEADER", "ALIGNED_BYTES",
    "BYTES",           "CALL",            "RET",          "JUMP",
    "BRANCH",          "BRANCH_TARGET",   "COMPARE",      "COMPARE_ZERO",
    "NOT",             "LOGIC",           "LOGIC3",       "BITFIELD_EXTRACT",
    "BITFIELD_INSERT", "BITFIELD_CLEAR",  "COUNT_ZEROS",  "POPCNT",
    "BIT_TEST",        "MIN",             "MAX",          "ADD",
    "SUB",             "NEG",             "ADDR",         "MUL",
    "DIV",             "IMULHI",          "IDIV",         "IMOD",
    "ABS",             "RCP",             "SQRT",         "RSQRT",
    "EXP2",            "CONVERT",         "FIXUP",        "SHUFFLE",
    "IGNORE_LANES",    "BLEND",           "BLEND_ZERO",   "PREFETCH",
    "LOAD",            "EXPAND",          "GATHER",       "INSERT",
    "BROADCAST",       "STORE",           "COMPRESS",     "SCATTER",
    "EXTRACT",         "ATOMIC_ADD",      "ATOMIC_SUB",   "ATOMIC_LOGIC",
    "ATOMIC_XCHG",     "ATOMIC_CMP_XCHG", "MEMSET",       "MEMCPY",
    "NUM_OPCODES"};

void printDebug(EventBuilder builder, size_t numEvents) {
  size_t offset = (numEvents + 2) / 3;
  for (size_t i = offset, e = numEvents + offset; i != e; ++i) {
    if (builder.data(i) >= offset && builder.data(i) < e)
      printf("%3d : %8d > %s\n", i, builder.data(i),
             opcodeNames[builder.kind(i)]);
    else
      printf("%3d : %08x > %s\n", i, builder.data(i),
             opcodeNames[builder.kind(i)]);
  }
}

}  // namespace Jagger {
#endif
