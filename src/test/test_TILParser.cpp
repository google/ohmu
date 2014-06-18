//===- test_TILParser.cpp --------------------------------------*- C++ --*-===//
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

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "parser/DefaultLexer.h"
#include "parser/BNFParser.h"
#include "parser/TILParser.h"
#include "til/CFGReducer.h"

#include <iostream>

using namespace ohmu;
using namespace ohmu::parsing;
using namespace clang::threadSafety;

class TILPrinter : public til::PrettyPrinter<TILPrinter, std::ostream> {
public:
  TILPrinter() : PrettyPrinter(false, false) { }
};

void printSExpr(til::SExpr* e) {
  TILPrinter::print(e, std::cout);
}

namespace Try2 {

typedef int EventIndex;

struct LiveRange {
  EventIndex origin; // instruction
  EventIndex begin;
  EventIndex end; // instruction
  int pressure;
  unsigned registerSet;
  unsigned clobberedSet;
  BasicBlock* block;
  EventIndex nextUse;
};

struct DuplicateLiveRange {
  EventIndex liveRange;
};

struct IntLiteral {
  int value;
  EventIndex lastUse;
};

struct Instruction {
  enum OpCode { NOP, JMP, BRANCH, ADD, MUL, CMP_EQ, CMP_LT, CMP_LE };
  OpCode opcode;
  int numArgs;
  int uses;
  unsigned registerSet;
  EventIndex marker;
  EventIndex lastUse;
};

struct BlockHeader {
  int previousPostDominatorID;
  EventIndex parentIndex;
  int postDominatorRangeBegin;
  int postDominatorRangeEnd;
  EventIndex nextBlockIndex;
};

struct Event {
  enum Kind { LIVE_RANGE, DUPLICATE_LIVE_RANGE, INT_LITERAL, INSTRUCTION, BLOCK_HEADER };
  Kind kind;
  union {
    LiveRange liveRange;
    DuplicateLiveRange duplicateLiveRange;
    IntLiteral intLiteral;
    Instruction instruction;
    BlockHeader blockHeader;
  };
  static Event makeLiveRange(EventIndex origin, EventIndex end, BasicBlock* block) {
    LiveRange liveRange = { origin, origin, end, 0, 0, 0, block };
    Event event = { LIVE_RANGE };
    event.liveRange = liveRange;
    return event;
  }
  static Event makeDuplicateLiveRange(EventIndex liveRange) {
    DuplicateLiveRange duplicateLiveRange = { liveRange };
    Event event = { DUPLICATE_LIVE_RANGE };
    event.duplicateLiveRange = duplicateLiveRange;
    return event;
  }
  static Event makeIntLiteral(int value) {
    IntLiteral intLiteral = { value };
    Event event = { INT_LITERAL };
    event.intLiteral = intLiteral;
    return event;
  }
  static Event makeInstruction(Instruction::OpCode opcode, int numArgs) {
    Instruction instruction = { opcode, numArgs, 0, 0, 0 };
    Event event = { INSTRUCTION };
    event.instruction = instruction;
    return event;
  }
  static Event makeBlockHeader(int previousPostDominatorID, EventIndex parentIndex, int postDominatorRangeBegin, int postDominatorRangeEnd) {
    BlockHeader blockHeader = { previousPostDominatorID, parentIndex, postDominatorRangeBegin, postDominatorRangeEnd };
    Event event = { BLOCK_HEADER };
    event.blockHeader = blockHeader;
    return event;
  }
  void print(Event* events);
};

struct RegisterAllocator {
  std::vector<Event> events;
  int getNewID() const { return (int)events.size(); }
  int getLastID() const { return (int)events.size() - 1; }

  void tallyPressure(BasicBlock* block, Event* event);
  void tallyPressureHelper(BasicBlock* block, Event* event);
  unsigned testRegisters(BasicBlock* block, Event* event);
  unsigned testRegistersHelper(BasicBlock* block, Event* event);
  void markRegisters(BasicBlock* block, Event* event);
  void markRegistersHelper(BasicBlock* block, Event* event);

  explicit RegisterAllocator(SCFG* cfg);
  void emitTerminator(BasicBlock* basicBlock);
  void emitJump(BasicBlock* basicBlock, Goto* jump);
  void emitBranch(BasicBlock* basicBlock, Branch* branch);
  void emitLiteral(Literal* literal);
  void emitBinaryOp(BasicBlock* basicBlock, BinaryOp* binaryOp);
  void emitExpression(BasicBlock* basicBlock, SExpr* expr);
  void print();
};

void RegisterAllocator::tallyPressure(BasicBlock* block, Event* event) {
  if (event->liveRange.begin >= block->VX64BlockStart) {
    for (int i = event->liveRange.begin + 1, e = event->liveRange.end; i != e; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        //printf("! %d - %d - %d[%d]\n", event->liveRange.begin, i, e, event->liveRange.end);
        events[i].liveRange.pressure++;
        event->liveRange.pressure++;
      }
    return;
  }
  // Assumes no live ranges *start* in the exit block.
  for (int i = event->liveRange.end - 1; events[i].kind != Event::BLOCK_HEADER; --i)
    if (events[i].kind == Event::LIVE_RANGE) {
      events[i].liveRange.pressure++;
      event->liveRange.pressure++;
    }
  block->Marker = event - events.data();
  for (auto pred : block->predecessors())
    tallyPressureHelper(pred, event);
}

void RegisterAllocator::tallyPressureHelper(BasicBlock* block, Event* event) {
  if (block->Marker == event - events.data())
    return;
  block->Marker = event - events.data();
  //printf("! %d %d %d\n", block->blockID(), marker, begin);
  //printf("%d - %d\n", block->VX64BlockStart, block->VX64BlockEnd);
  if (block->VX64BlockStart <= event->liveRange.begin && event->liveRange.begin < block->VX64BlockEnd) {
    for (int i = event->liveRange.begin + 1; events[i].kind != Event::BLOCK_HEADER; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        events[i].liveRange.pressure++;
        event->liveRange.pressure++;
      }
      return;
  }
  for (int i = block->VX64BlockStart, e = block->VX64BlockEnd; i != e; ++i)
    if (events[i].kind == Event::LIVE_RANGE) {
      events[i].liveRange.pressure++;
      event->liveRange.pressure++;
    }
  for (auto pred : block->predecessors())
    tallyPressureHelper(pred, event);
}

unsigned RegisterAllocator::testRegisters(BasicBlock* block, Event* event) {
  unsigned registerSet = 0xffffffff;
  if (event->liveRange.begin >= block->VX64BlockStart) {
    for (int i = event->liveRange.begin + 1, e = event->liveRange.end; i != e; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        //printf(">%d : %d : %03x\n", event - events.data(), i, events[i].liveRange.registerSet);
        registerSet &= ~events[i].liveRange.registerSet;
      }
    return registerSet;
  }
  // Assumes no live ranges *start* in the exit block.
  //printf("%d - %d - %d\n", event->liveRange.begin, event - events.data(), event->liveRange.end);
  for (int i = event->liveRange.end - 1; events[i].kind != Event::BLOCK_HEADER; --i)
    if (events[i].kind == Event::LIVE_RANGE) {
      //printf(">%d : %d : %03x\n", event - events.data(), i, events[i].liveRange.registerSet);
      registerSet &= ~events[i].liveRange.registerSet;
    }
  block->Marker = event - events.data();
  for (auto pred : block->predecessors())
    registerSet &= testRegisters(pred, event);
  return registerSet;
}

unsigned RegisterAllocator::testRegistersHelper(BasicBlock* block, Event* event) {
  unsigned registerSet = 0xffffffff;
  if (block->Marker == event - events.data())
    return registerSet;
  block->Marker = event - events.data();
  //printf("! %d %d %d\n", block->blockID(), marker, begin);
  //printf("%d - %d\n", block->VX64BlockStart, block->VX64BlockEnd);
  if (block->VX64BlockStart <= event->liveRange.begin && event->liveRange.begin < block->VX64BlockEnd) {
    for (int i = event->liveRange.begin + 1; events[i].kind != Event::BLOCK_HEADER; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        //printf(">%d : %d : %03x\n", marker, i, events[i].liveRange.registerSet);
        registerSet &= ~events[i].liveRange.registerSet;
      }
    return registerSet;
  }
  for (int i = block->VX64BlockStart, e = block->VX64BlockEnd; i != e; ++i)
    if (events[i].kind == Event::LIVE_RANGE) {
      //printf(">%d : %d : %03x\n", marker, i, events[i].liveRange.registerSet);
      registerSet &= ~events[i].liveRange.registerSet;
    }
  for (auto pred : block->predecessors())
    registerSet &= testRegistersHelper(pred, event);
  return registerSet;
}

void RegisterAllocator::markRegisters(BasicBlock* block, Event* event) {
  if (event->liveRange.begin >= block->VX64BlockStart) {
    for (int i = event->liveRange.begin + 1, e = event->liveRange.end; i != e; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        //printf(">%d : %d : %03x\n", event - events.data(), i, events[i].liveRange.registerSet);
        events[i].liveRange.clobberedSet |= event->liveRange.registerSet;
      }
    return;
  }
  // Assumes no live ranges *start* in the exit block.
  //printf("%d - %d - %d\n", event->liveRange.begin, event - events.data(), event->liveRange.end);
  for (int i = event->liveRange.end - 1; events[i].kind != Event::BLOCK_HEADER; --i)
    if (events[i].kind == Event::LIVE_RANGE) {
      //printf(">%d : %d : %03x\n", event - events.data(), i, events[i].liveRange.registerSet);
      events[i].liveRange.clobberedSet |= event->liveRange.registerSet;
    }
  block->Marker = event - events.data();
  for (auto pred : block->predecessors())
     markRegisters(pred, event);
}

void RegisterAllocator::markRegistersHelper(BasicBlock* block, Event* event) {
  if (block->Marker == event - events.data())
    return;
  block->Marker = event - events.data();
  //printf("! %d %d %d\n", block->blockID(), marker, begin);
  //printf("%d - %d\n", block->VX64BlockStart, block->VX64BlockEnd);
  if (block->VX64BlockStart <= event->liveRange.begin && event->liveRange.begin < block->VX64BlockEnd) {
    for (int i = event->liveRange.begin + 1; events[i].kind != Event::BLOCK_HEADER; ++i)
      if (events[i].kind == Event::LIVE_RANGE) {
        //printf(">%d : %d : %03x\n", marker, i, events[i].liveRange.registerSet);
        events[i].liveRange.clobberedSet |= event->liveRange.registerSet;
      }
    return;
  }
  for (int i = block->VX64BlockStart, e = block->VX64BlockEnd; i != e; ++i)
    if (events[i].kind == Event::LIVE_RANGE) {
      //printf(">%d : %d : %03x\n", marker, i, events[i].liveRange.registerSet);
      events[i].liveRange.clobberedSet |= event->liveRange.registerSet;
    }
  for (auto pred : block->predecessors())
    markRegistersHelper(pred, event);
}

extern "C" unsigned char _BitScanForward(unsigned long * Index, unsigned long Mask);

static int log2(int x) {
  unsigned long a;
  _BitScanForward(&a, (unsigned long)x);
  return (int)a;
}

static void printArgument(Event* events, Event& event) {
  switch (event.kind) {
  case Event::LIVE_RANGE:
    printf("%d", log2(event.liveRange.registerSet));
    break;
  case Event::DUPLICATE_LIVE_RANGE:
    printArgument(events, events[event.duplicateLiveRange.liveRange]);
    break;
  case Event::INT_LITERAL:
    printf("(%d)", event.intLiteral.value);
    break;
  }
}

static void printRename(Event* events, Event& event) {
  if (event.kind != Event::LIVE_RANGE)
    return;
  if (!event.liveRange.nextUse)
    return;
  printf(" [%d -> %d]", log2(event.liveRange.registerSet), log2(events[event.liveRange.nextUse].liveRange.registerSet));
}

static void printInstruction(Event* events, Event& event) {
  switch (event.instruction.opcode) {
  case Instruction::ADD:
    printf("ADD ");
    printArgument(events, (&event)[-2]);
    printf(" ");
    printArgument(events, (&event)[-1]);
    printf(" -> %d", log2(events[event.instruction.lastUse].liveRange.registerSet));
    printRename(events, (&event)[-2]);
    printRename(events, (&event)[-1]);
    printf("\n");
    break;
  case Instruction::MUL:
    printf("MUL ");
    printArgument(events, (&event)[-2]);
    printf(" ");
    printArgument(events, (&event)[-1]);
    printf(" -> %d", log2(events[event.instruction.lastUse].liveRange.registerSet));
    printRename(events, (&event)[-2]);
    printRename(events, (&event)[-1]);
    printf("\n");
    break;
  case Instruction::CMP_EQ:
    printf("CMPEQ ");
    printArgument(events, (&event)[-2]);
    printf(" ");
    printArgument(events, (&event)[-1]);
    printf(" -> %d", log2(events[event.instruction.lastUse].liveRange.registerSet));
    printRename(events, (&event)[-2]);
    printRename(events, (&event)[-1]);
    printf("\n");
    break;
  case Instruction::JMP:
    printf("JMP\n");
    break;
  case Instruction::BRANCH:
    printf("JMPCC %d\n", log2((&event)[-1].liveRange.registerSet));
    break;
  }
}

RegisterAllocator::RegisterAllocator(SCFG* cfg) {
  BasicBlock* previousBlock = nullptr;
  for (auto block : *cfg) {
    int blockHeaderIndex = getNewID();
    events.push_back(Event::makeBlockHeader(
      previousBlock ? previousBlock->PostDominatorNode.NodeID : 0,
      block->parent() ? block->parent()->VX64BlockEnd : 0,
      block->PostDominatorNode.NodeID,
      block->PostDominatorNode.NodeID + block->PostDominatorNode.SizeOfSubTree));
    block->VX64BlockStart = blockHeaderIndex;
    for (auto arg : block->arguments())
      arg->definition()->setId(blockHeaderIndex);
    for (auto instr : block->instructions())
      emitExpression(block, instr);
    emitTerminator(block);
    block->VX64BlockEnd = getNewID();
    events[blockHeaderIndex].blockHeader.nextBlockIndex = block->VX64BlockEnd;
    previousBlock = block;
  }
  //events.push_back(Event::makeBlockHeader(0, 0, 0, 0));

#if 1
  // Compute def-use pairs.
  for (auto& event : events) {
    if (event.kind != Event::LIVE_RANGE)
      continue;
    //event.print(events.data());
    //printf("\n");
    for (Event* i = &event - 1, *e = &events[event.liveRange.origin]; i != e; --i) {
      if (i->kind == Event::BLOCK_HEADER) {
        i = events.data() + i->blockHeader.parentIndex;
        continue;
      }
      if (i->kind == Event::LIVE_RANGE && i->liveRange.origin == event.liveRange.origin) {
        event.liveRange.begin = i->liveRange.end;
        i->liveRange.nextUse = &event - events.data();
        goto DONE;
      }
    }
    if (events[event.liveRange.origin].kind == Event::INSTRUCTION)
      events[event.liveRange.origin].instruction.lastUse = &event - events.data();
    else if (events[event.liveRange.origin].kind == Event::INT_LITERAL)
      events[event.liveRange.origin].intLiteral.lastUse = &event - events.data();
    else printf("!!!!!!!%d!!!!!!\n", events[event.liveRange.origin].kind);
  DONE:
    ;
    // event.print(events.data());
    // printf("\n\n");
  }
#endif

  // Compute pressure generated by the pairs.
  for (auto block : *cfg) {
    for (auto event = events.data() + block->VX64BlockStart, e = events.data() + block->VX64BlockEnd; event != e; ++event) {
      if (event->kind != Event::LIVE_RANGE)
        continue;
      tallyPressure(event->liveRange.block, event);
    }
  }

  for (auto block : *cfg)
    block->Marker = 0;

  std::vector<Event*> ptrs;
  for (auto& event : events)
    if (event.kind == Event::LIVE_RANGE)
      ptrs.push_back(&event);
  std::sort(ptrs.begin(), ptrs.end(), [](Event *a, Event *b) {return a->liveRange.pressure < b->liveRange.pressure; });
  for (auto ptr : ptrs) {
    auto registerSet = testRegisters(ptr->liveRange.block, ptr) & ~ptr->liveRange.clobberedSet;
    printf("%d->%d | %d %08x : %08x\n", ptr->liveRange.begin, ptr->liveRange.end, ptr->liveRange.pressure, registerSet, registerSet & -registerSet);
    ptr->liveRange.registerSet = registerSet & -registerSet;
    markRegisters(ptr->liveRange.block, ptr);
  }

  for (auto& event : events)
    if (event.kind == Event::INT_LITERAL)
      printf("[%d] -> %d\n", event.intLiteral.value, log2(events[event.intLiteral.lastUse].liveRange.registerSet));
    else if (event.kind == Event::INSTRUCTION)
      printInstruction(events.data(), event);
}

void RegisterAllocator::emitLiteral(Literal* literal) {
  switch (literal->valueType().Base) {
  case ValueType::BT_Int: events.push_back(Event::makeIntLiteral(literal->as<int>().value())); break;
  default:
    assert(false);
  }
}

void RegisterAllocator::emitBinaryOp(BasicBlock* basicBlock, BinaryOp* binaryOp) {
  Instruction::OpCode opcode = Instruction::NOP;
  switch (binaryOp->binaryOpcode()) {
  case BOP_Add: opcode = Instruction::ADD; break;
  case BOP_Mul: opcode = Instruction::MUL; break;
  case BOP_Eq: opcode = Instruction::CMP_EQ; break;
  case BOP_Lt: opcode = Instruction::CMP_LT; break;
  case BOP_Leq: opcode = Instruction::CMP_LE; break;
  }
  SExpr* expr0 = binaryOp->expr0();
  SExpr* expr1 = binaryOp->expr1();
  emitExpression(basicBlock, expr0);
  emitExpression(basicBlock, expr1);
  int site = getNewID();
  //if (expr0->opcode() == COP_Literal) {
  //  emitLiteral(cast<Literal>(expr0));
  //  printf("<<???>>");
  //}
  //else
    events.push_back(Event::makeLiveRange(expr0->id(), site + 2, basicBlock));
  if (expr1 != expr0)
    //if (expr1->opcode() == COP_Literal) {
    //  emitLiteral(cast<Literal>(expr1));
    //  printf("<<!@#>>");
    //}
    //else
      events.push_back(Event::makeLiveRange(expr1->id(), site + 2, basicBlock));
  else
    events.push_back(Event::makeDuplicateLiveRange(site));
  events.push_back(Event::makeInstruction(opcode, 2));
}

void RegisterAllocator::emitExpression(BasicBlock* basicBlock, SExpr* expr) {
  if (expr->id())
    return;
  switch (expr->opcode()) {
  case COP_Literal: emitLiteral(cast<Literal>(expr)); break;
  case COP_Variable: emitExpression(basicBlock, cast<Variable>(expr)->definition()); break;
  case COP_BinaryOp: emitBinaryOp(basicBlock, cast<BinaryOp>(expr)); break;
  }
  expr->setId(getLastID());
}

void RegisterAllocator::emitTerminator(BasicBlock* basicBlock) {
  auto expr = basicBlock->terminator();
  if (expr) {
    switch (expr->opcode()) {
    case COP_Goto: emitJump(basicBlock, cast<Goto>(expr)); break;
    case COP_Branch: emitBranch(basicBlock, cast<Branch>(expr)); break;
    }
    expr->setId(getLastID());
  }
}

// the index for this block in the target's phis
static size_t getPhiIndex(BasicBlock* basicBlock, BasicBlock* targetBlock) {
  auto& predecessors = targetBlock->predecessors();
  for (size_t i = 0, e = predecessors.size(); i != e; ++i)
    if (predecessors[i] == basicBlock)
      return i;
  return 0;
}

void RegisterAllocator::emitJump(BasicBlock* basicBlock, Goto* jump) {
  auto targetBlock = jump->targetBlock();
  size_t phiIndex = getPhiIndex(basicBlock, targetBlock);
  auto& arguments = targetBlock->arguments();
  int site = getNewID();
  int numArgs = arguments.size();
  for (auto arg : arguments)
    emitExpression(basicBlock, cast<Phi>(arg->definition())->values()[phiIndex]);
  for (auto arg : arguments)
    events.push_back(Event::makeLiveRange(cast<Phi>(arg->definition())->values()[phiIndex]->id(), site + numArgs, basicBlock));
  events.push_back(Event::makeInstruction(Instruction::JMP, numArgs));
}

void RegisterAllocator::emitBranch(BasicBlock* basicBlock, Branch* branch) {
  auto thenBlock = branch->thenBlock();
  auto elseBlock = branch->elseBlock();
  size_t thenPhiIndex = getPhiIndex(basicBlock, thenBlock);
  size_t elsePhiIndex = getPhiIndex(basicBlock, elseBlock);
  auto& thenArguments = thenBlock->arguments();
  auto& elseArguments = elseBlock->arguments();
  // Emit the expressions if they haven't yet been.
  for (auto arg : thenArguments) emitExpression(basicBlock, cast<Phi>(arg->definition())->values()[thenPhiIndex]);
  for (auto arg : elseArguments) emitExpression(basicBlock, cast<Phi>(arg->definition())->values()[elsePhiIndex]);
  emitExpression(basicBlock, branch->condition());
  // Emit the call site for this branch, including all phi arguments.
  int site = getNewID();
  for (auto arg : thenArguments) {
    SExpr *expr = cast<Phi>(arg->definition())->values()[thenPhiIndex];
    events[expr->id()].instruction.marker = site;
    events.push_back(Event::makeLiveRange(expr->id(), site, basicBlock));
  }
  for (auto arg : elseArguments) {
    SExpr *expr = cast<Phi>(arg->definition())->values()[elsePhiIndex];
    if (events[expr->id()].instruction.marker == site)
      continue;
    events[expr->id()].instruction.marker = site;
    events.push_back(Event::makeLiveRange(expr->id(), site, basicBlock));
  }
  int numArgs = getNewID() - site + 1;
  int end = site + numArgs;
  for (size_t i = (size_t)site, e = (size_t)(end - 1); i != e; ++i)
    events[i].liveRange.end = end;
  events.push_back(Event::makeLiveRange(branch->condition()->id(), end, basicBlock));
  events.push_back(Event::makeInstruction(Instruction::BRANCH, numArgs));
}

void Event::print(Event* events) {
  switch (kind) {
  case Event::LIVE_RANGE:
    printf("[%d] %d -> %d : %d : %x", liveRange.origin, liveRange.begin, liveRange.end, liveRange.pressure, liveRange.registerSet);
    break;
  case Event::DUPLICATE_LIVE_RANGE:
    events[duplicateLiveRange.liveRange].print(events);
    break;
  case Event::INT_LITERAL:
    printf("%d", intLiteral.value);
    break;
  case Event::INSTRUCTION:
    switch (instruction.opcode) {
    case Instruction::NOP: printf("0"); break;
    case Instruction::JMP: printf("JMP"); break;
    case Instruction::BRANCH: printf("BRANCH"); break;
    case Instruction::ADD: printf("+"); break;
    case Instruction::MUL: printf("*"); break;
    case Instruction::CMP_EQ: printf("=="); break;
    case Instruction::CMP_LT: printf("<"); break;
    case Instruction::CMP_LE: printf("<="); break;
    };
    printf(" : numArgs=%d uses=%d", instruction.numArgs, instruction.uses);
    break;
  case Event::BLOCK_HEADER:
    printf("Block %d %d : %d %d %d", blockHeader.previousPostDominatorID, blockHeader.parentIndex, blockHeader.nextBlockIndex, blockHeader.postDominatorRangeBegin, blockHeader.postDominatorRangeEnd);
    //printf("Block %d : size=%d min=%d max=%d\n", blockHeader.blockID, blockHeader.blockSize, blockHeader.dominatorRangeBeing, blockHeader.dominatorRangeEnd);
    break;
  }
}

void RegisterAllocator::print() {
  int i = 0;
  for (auto& event : events) {
    printf("%s%-2d: ", event.kind == Event::BLOCK_HEADER ? "\n" : "", i++);
    event.print(events.data());
    printf("\n");
  }
}

} // namespace Try2

#if 0
namespace BackEnd {
struct Instruction;

struct Argument {
  enum Kind { REGISTER, LITERAL_INT, COPY_1 };
  explicit Argument(Kind kind)
      : kind(kind), nextUseOffset(0), keyRangeOffset(0) {}
  explicit Argument(int literal)
      : kind(LITERAL_INT), data(literal), nextUseOffset(0), keyRangeOffset(0) {}
  Argument(int defIndex, int useIndex)
      : kind(REGISTER), keyRangeOffset(0), nextUseOffset(0),
        rangeOrigin(defIndex), rangeBegin(defIndex), rangeEnd(useIndex),
        interference(0), registerSet(-1u) {}

  Kind kind;
  int keyRangeOffset; // relative to this
  int nextUseOffset;  // relative to this
  int rangeOrigin; // absolute in instrs
  int rangeBegin; // absolute in instrs
  int rangeEnd;   // absolute in instrs
  int interference;
  unsigned registerSet;
  int data;
};

struct Instruction {
  enum Opcode { NOP, BLOCK_SENTINAL, JMP, BRANCH, ADD, MUL, CMP_EQ, CMP_LT, CMP_LE };
  Instruction(Opcode opcode, int argsBegin, int argsSize, int offsetToParent = -1)
      : opcode(opcode), argsBegin(argsBegin), argsSize(argsSize), uses(0),
        offsetToParent(offsetToParent), externalPressure(0), registerSet(-1u),
        clobberSet(0) {}

  Opcode opcode;
  int argsBegin; // absolute in args
  int argsSize;  // absolute
  int offsetToParent; // relative to this
  int offsetToFirstUse; // relative to this
  int externalPressure;
  int uses;
  unsigned registerSet;
  unsigned clobberSet;
  Instruction* parent() { return this + offsetToParent; }
  int generatedLiveRanges(const Argument* args) const {
    return uses;
  }

  int consumedLiveRanges(const Argument* args) const {
    int result = 0;
    for (auto i = argsBegin, e = argsBegin + argsSize; i != e; ++i)
      if (args[i].kind == Argument::REGISTER)
        result++;
    return result;
  }
};

struct RegisterAllocator {
  std::vector<Instruction> instrs;
  std::vector<Argument> args;

  explicit RegisterAllocator(SCFG* cfg);
  void emitEntry(BasicBlock* basicBlock);
  void emitExit(BasicBlock* basicBlock);
  SExpr* emitExpression(SExpr* expr);
  //void initializeLiveRange(Instruction* instr);
  void print();
};

RegisterAllocator::RegisterAllocator(SCFG* cfg) {
  for (auto basicBlock : *cfg) {
    emitEntry(basicBlock);
    for (auto instr : basicBlock->instructions())
      emitExpression(instr->definition());
    emitExit(basicBlock);
  }
  for (auto& arg : args) {
    if (arg.kind != Argument::REGISTER)
      continue;
    //printf("arg : %d--%d (%d)\n", arg.rangeBegin, arg.rangeEnd, instrs[arg.rangeEnd].parent() - &instrs[arg.rangeBegin]);
    //printf("> %d - %d\n", arg.rangeBegin, arg.rangeEnd);
    for (Instruction* i = instrs[arg.rangeEnd].parent(), *e = &instrs[arg.rangeBegin]; i != e; i = i->parent()) {
      for (auto a = i->argsBegin, ae = i->argsBegin + i->argsSize; a < ae; ++a) {
        if (args[a].kind == Argument::REGISTER && args[a].rangeOrigin == arg.rangeOrigin) {
          args[a].nextUseOffset = (int)(&arg - args.data()) - a;
          arg.rangeBegin = i - instrs.data();
          goto DONE;
        }
      }
      i->externalPressure++;
    }
    //printf("  %d - %d\n", arg.rangeBegin, arg.rangeEnd);
  DONE:;
    instrs[arg.rangeBegin].uses++;
    //printf("    : %d--%d\n", arg.rangeBegin, arg.rangeEnd);
  }
  for (auto& arg : args) {
    if (arg.kind != Argument::REGISTER)
      continue;
    //if (arg.nextUseOffset)
    //  continue;
    int temp0 = instrs[arg.rangeEnd].externalPressure + instrs[arg.rangeEnd].consumedLiveRanges(args.data()) - 1;
    int temp1 = instrs[arg.rangeBegin].externalPressure + instrs[arg.rangeBegin].generatedLiveRanges(args.data()) - 1;
    for (Instruction* i = instrs[arg.rangeEnd].parent(), *e = &instrs[arg.rangeBegin]; i != e; i = i->parent()) {
      temp0 += i->consumedLiveRanges(args.data());
      if (i->opcode != Instruction::JMP && i->opcode != Instruction::BRANCH)
        temp1 += i->generatedLiveRanges(args.data());
    }
    arg.interference = temp1;
    printf("%d->%d | %d : %d\n", arg.rangeBegin, arg.rangeEnd, temp0, temp1);
  }
  //for (auto instr : instrs) {
  //}
  std::vector<Argument*> ptrs;
  for (auto& arg : args)
    if (arg.kind == Argument::REGISTER)
    ptrs.push_back(&arg);
  std::sort(ptrs.begin(), ptrs.end(), [](Argument* a, Argument* b){return a->interference < b->interference;});
  for (auto arg : ptrs) {
    unsigned registerSet = 0xffffffff;
    printf("%d->%d | %d\n", arg->rangeBegin, arg->rangeEnd, arg->interference);
  }
}

static Argument convertToArgument(SExpr* expr, int instrID) {
  if (expr->opcode() == COP_Literal) {
    assert(cast<Literal>(expr)->valueType().Base == ValueType::BT_Int);
    return Argument(cast<Literal>(expr)->as<int>().value());
  }
  return Argument(expr->id(), instrID);
}

void RegisterAllocator::emitEntry(BasicBlock* basicBlock) {
  int instrID = (int)instrs.size();
  for (auto arg : basicBlock->arguments())
    arg->definition()->setId(instrID);
  int offsetToPrevious = (/*basicBlock->parent() ? basicBlock->parent()->VX64InstrsExit :*/ 0) - instrID;
  instrs.push_back(Instruction(Instruction::BLOCK_SENTINAL, (int)args.size(), 0, offsetToPrevious));
}

SExpr* RegisterAllocator::emitExpression(SExpr* expr) {
  while (expr->opcode() == COP_Variable)
    expr = cast<Variable>(expr)->definition();
  if (expr->id())
    return expr;
  if (expr->opcode() == COP_Literal)
    return expr;
  switch (expr->opcode()) {
  case COP_BinaryOp: {
    auto binOp = cast<BinaryOp>(expr);
    Instruction::Opcode opcode = Instruction::NOP;
    switch (binOp->binaryOpcode()) {
    case BOP_Add: opcode = Instruction::ADD; break;
    case BOP_Mul: opcode = Instruction::MUL; break;
    case BOP_Eq: opcode = Instruction::CMP_EQ; break;
    case BOP_Lt: opcode = Instruction::CMP_LT; break;
    case BOP_Leq: opcode = Instruction::CMP_LE; break;
    }
    SExpr* expr0 = emitExpression(binOp->expr0());
    SExpr* expr1 = emitExpression(binOp->expr1());
    int instrID = (int)instrs.size();
    instrs.push_back(Instruction(opcode, (int)args.size(), 2, -1));
    args.push_back(convertToArgument(expr0, instrID));
    if (expr0 != expr1)
      args.push_back(convertToArgument(expr1, instrID));
    else
      args.push_back(Argument(Argument::COPY_1));
    expr->setId(instrID);
    return expr;
  }}
  return expr;
}

void RegisterAllocator::emitExit(BasicBlock* basicBlock) {
  auto expr = basicBlock->terminator();
  if (!expr) {
    instrs.push_back(Instruction(Instruction::BLOCK_SENTINAL, args.size(), 0));
    return;
  }

  int argID = (int)args.size();
  int instrID = (int)instrs.size();
  expr->setId(instrID);
  switch (expr->opcode()) {
  case COP_Branch: {
    auto branch = cast<Branch>(expr);
    SExpr* expr = emitExpression(branch->condition());
    argID = (int)args.size();
    instrID = (int)instrs.size();
    instrs.push_back(Instruction(Instruction::BRANCH, argID, 1));
    args.push_back(convertToArgument(expr, instrID));
    argID = (int)args.size();
    instrID = (int)instrs.size();
    size_t i = 0;
    for (size_t e = branch->thenBlock()->predecessors().size(); i != e; ++i)
      if (branch->thenBlock()->predecessors()[i] == basicBlock)
        break;
    for (auto arg : branch->thenBlock()->arguments()) {
      SExpr *expr = emitExpression(cast<Phi>(arg->definition())->values()[i]); // should never actually emit anything
      instrs[expr->id()].offsetToFirstUse = instrID - expr->id();
      args.push_back(convertToArgument(expr, instrID));
    }
    i = 0;
    for (size_t e = branch->elseBlock()->predecessors().size(); i != e; ++i)
      if (branch->elseBlock()->predecessors()[i] == basicBlock)
        break;
    for (auto arg : branch->elseBlock()->arguments()) {
      SExpr *expr = emitExpression(cast<Phi>(arg->definition())->values()[i]); // should never actually emit anything
      if (instrs[expr->id()].offsetToFirstUse == instrID - expr->id())
        continue;
      instrs[expr->id()].offsetToFirstUse = instrID - expr->id();
      args.push_back(convertToArgument(expr, instrID));
    }

    break;
  }
  case COP_Goto: {
    auto goto_ = cast<Goto>(expr);
    instrs.push_back(Instruction(Instruction::JMP, argID, 0));
    instrID = (int)instrs.size();
    size_t i = 0;
    for (size_t e = goto_->targetBlock()->predecessors().size(); i != e; ++i)
      if (goto_->targetBlock()->predecessors()[i] == basicBlock)
        break;
    for (auto arg : cast<Goto>(expr)->targetBlock()->arguments()) {
      args.push_back(convertToArgument(emitExpression(cast<Phi>(arg->definition())->values()[i]), instrID));
    }
      break;
  }
  }

  //basicBlock->VX64InstrsExit = instrID;
  instrs.push_back(Instruction(Instruction::BLOCK_SENTINAL, argID, args.size() - argID));
}

void RegisterAllocator::print() {
  int i = 0;
  for (auto x : instrs) {
    printf("%2d->(%2d)[%d][%d]|%d:", i++, -x.offsetToParent, x.externalPressure, x.uses, x.opcode);
    for (int a = x.argsBegin, e = x.argsBegin + x.argsSize; a != e; ++a)
      if (args[a].kind != Argument::REGISTER)
        printf(" (%d)", args[a].data);
      else
        printf(" %d->%d", args[a].rangeBegin, args[a].rangeEnd);
    printf("\n");
  }
}

#if 0

void allocateSpillSlots() {}

void fillOutSpillArraysAndAllocateForcedRegisters(Argument *arg, Instr *use) {}

void doForcedSpills() {}

void computeNumAdjacentRanges() {}

void allocateRegister() {}

void print(Literal *literal) { printf("%d\n", literal->intLiteral); }

void print(Instr *instr) {
  printf("%d : %d %d\n", instr->opcode, instr->arg0->kind, instr->arg1->kind);
}

void print(Phi *phi) {
  for (size_t i = 0, e = phi->numArgs; i != e; ++i)
    printf("(%p %d) ", phi->args[i].block, phi->args[i].value->kind);
  printf("\n");
}

void print(Block *block) {
  printf("phis:\n");
  for (size_t i = 0, e = block->numPhis; i != e; ++i)
    print(block->phis + i);
  for (size_t i = 0, e = block->numInstrs; i != e; ++i)
    print(block->instrs + i);
  printf("\n");
}
void print(CFG *cfg) {
  for (size_t i = 0, e = cfg->numLiterals; i != e; ++i)
    print(cfg->literals + i);
  for (size_t i = 0, e = cfg->numBlocks; i != e; ++i)
    print(cfg->blocks[i]);
}

Block *Make(BasicBlock *block) {
  auto x = new Block();
  x->
}

CFG *Make(SCFG *cfg) {
  auto x = new CFG();
  x->numBlocks = 0;
  for (auto i : *cfg)
    x->numBlocks++;
  x->blocks = x->numBlocks ? new Block *[x->numBlocks] : nullptr;
  size_t i = 0;
  for (auto block : *cfg)
    x->blocks[i++] = Make(block);
}

void Walk(BasicBlock *Block) {
  printf("Block%d\n", Block->blockID());
  for (auto i : Block->arguments())
    printf("  Arg   = %d\n", i->definition()->opcode());
  for (auto i : Block->instructions()) {
    printf("  Instr = %d\n", i->definition()->opcode());
    if (i->definition()->opcode() == COP_BinaryOp) {
      auto Def = cast<BinaryOp>(i->definition());
      if (Def->expr0()->opcode() == COP_Variable) {
        auto Var0 = cast<Variable>(Def->expr0());
        printf("    > %s%d_%d : %d\n", Var0->name().c_str(), Var0->getBlockID(),
               Var0->getID(), Var0->definition()->opcode());
      }
      if (Def->expr1()->opcode() == COP_Variable) {
        auto Var1 = cast<Variable>(Def->expr1());
        printf("    > %s%d_%d\n", Var1->name().c_str(), Var1->getBlockID(),
               Var1->getID());
      }
    }
  }
}

void Walk(SCFG *CFG) {
  for (auto i : *CFG)
    Walk(i);
}
#endif
} // namespace BackEnd
#endif

int main(int argc, const char** argv) {
  DefaultLexer lexer;
  TILParser tilParser(&lexer);


  const char* grammarFileName = "src/grammar/ohmu.grammar";
  FILE* file = fopen(grammarFileName, "r");
  if (!file) {
    std::cout << "File " << grammarFileName << " not found.\n";
    return -1;
  }

  bool success = BNFParser::initParserFromFile(tilParser, file, false);
  std::cout << "\n";
  //if (success)
  //  tilParser.printSyntax(std::cout);

  fclose(file);

  if (argc <= 1)
    return 0;

  // Read the ohmu file.
  auto *startRule = tilParser.findDefinition("definitions");
  if (!startRule) {
    std::cout << "Grammar does not contain rule named 'definitions'.\n";
    return -1;
  }

  file = fopen(argv[1], "r");
  if (!file) {
    std::cout << "File " << argv[1] << " not found.\n";
    return -1;
  }

  std::cout << "\nParsing " << argv[1] << "...\n";
  FileStream fs(file);
  lexer.setStream(&fs);
  // tilParser.setTrace(true);
  ParseResult result = tilParser.parse(startRule);
  if (tilParser.parseError())
    return -1;

  // Pretty print the parsed ohmu code.
  auto* v = result.getList<til::SExpr>(TILParser::TILP_SExpr);
  if (!v) {
    std::cout << "No definitions found.\n";
    return 0;
  }

  for (SExpr* e : *v) {
    std::cout << "\nDefinition:\n";
    printSExpr(e);
    std::cout << "\nCFG:\n";
    SCFG* cfg = CFGLoweringPass::convertSExprToCFG(e, tilParser.arena());
    cfg->computeNormalForm();
    //cfg->computeDominators();
    //cfg->computePostDominators();
    printSExpr(cfg);
    Try2::RegisterAllocator a(cfg);
    a.print();
  }
  delete v;

  std::cout << "\n";
  return 0;
}

