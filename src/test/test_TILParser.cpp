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
  unsigned copySet;
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

struct Predecessor {
	EventIndex branchIndex;
};

struct Event {
  enum Kind { LIVE_RANGE, DUPLICATE_LIVE_RANGE, INT_LITERAL, INSTRUCTION, BLOCK_HEADER, PREDECESSOR };
  Kind kind;
  union {
    LiveRange liveRange;
    DuplicateLiveRange duplicateLiveRange;
    IntLiteral intLiteral;
    Instruction instruction;
    BlockHeader blockHeader;
  };
  static Event makeLiveRange(EventIndex origin, EventIndex end, BasicBlock* block) {
    LiveRange liveRange = { origin, origin, end, 0, 0, 0, 0, block };
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
    Instruction instruction = { opcode, numArgs, 0, 0 };
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

static int log2(unsigned x) {
  unsigned long a;
  _BitScanReverse(&a, (unsigned long)x);
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
	//events[ptr->liveRange.begin].instruction.registerSet |= registerSet & -registerSet;
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
    printf(" : numArgs=%d", instruction.numArgs);
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

namespace Try3 {

struct Event {
  enum Kind {
    HEADER,
    LIVE_RANGE,
    DUPLICATE_LIVE_RANGE,
    INT_LITERAL,
    INSTRUCTION,
    WALK_BACK,
    SKIP_BACK,
    PHI_LINK
  };
  enum OpCode { NOP, JMP, BRANCH, ADD, MUL, CMP_EQ, CMP_LT, CMP_LE };
 
  Kind kind;
  unsigned copySet;
  union {
    struct {
      int offsetToOrigin; // instruction
      int pressure;
      unsigned registerSet; //< TODO: rename me
      unsigned clobberedSet;
    } liveRange;
    int offsetToTarget;
    int intLiteral;
    OpCode opcode;
    struct {
      int offsetToTarget;
      int key;
    } phiLink;
  };
  Event& setLiveRange(int offsetToOrigin) {
    kind = LIVE_RANGE;
    copySet = 0;
    liveRange.offsetToOrigin = offsetToOrigin;
    liveRange.pressure = 0;
    liveRange.registerSet = 0;
    liveRange.clobberedSet = 0;
    return *this;
  }
  Event& setOffsetToTarget(Kind kind, int offsetToTarget) {
    this->kind = kind;
    copySet = 0;
    this->offsetToTarget = offsetToTarget;
    return *this;
  }
  Event& setDuplicateLiveRange(int offsetToTarget) {
    return setOffsetToTarget(DUPLICATE_LIVE_RANGE, offsetToTarget);
  }
  Event& setIntLiteral(int value) {
    kind = INT_LITERAL;
    copySet = 0;
    intLiteral = value;
    return *this;
  }
  Event& setInstruction(OpCode opcode) {
    kind = INSTRUCTION;
    copySet = 0;
    this->opcode = opcode;
    return *this;
  }
  Event& setWalkBack(int offsetToTarget) {
    return setOffsetToTarget(WALK_BACK, offsetToTarget);
  }
  Event& setSkipBack(int offsetToTarget) {
    return setOffsetToTarget(SKIP_BACK, offsetToTarget);
  }
  Event& setPhiLink(int offsetToTarget, int key) {
    kind = PHI_LINK;
    copySet = 0;
    phiLink.offsetToTarget = offsetToTarget;
    phiLink.key = key;
    return *this;
  }
  Event& setHeader() {
    kind = HEADER;
    return *this;
  }
  void print(Event* events);
  void printASM(Event* events);
};

void Event::print(Event* events) {
  auto offset = this - events;
  switch (kind) {
  case Event::HEADER:
    printf("HEADER");
    break;
  case Event::LIVE_RANGE:
    printf("%d -> %d : %d : {%x} -> {%x}", offset + liveRange.offsetToOrigin, offset, liveRange.pressure, liveRange.registerSet, copySet);
    break;
  case Event::DUPLICATE_LIVE_RANGE:
    printf("COPY %d", offset + offsetToTarget);
    break;
  case Event::INT_LITERAL:
    printf("%d {%x}", intLiteral, copySet);
    break;
  case Event::INSTRUCTION:
    switch (opcode) {
    case Event::NOP: printf("0"); break;
    case Event::JMP: printf("JMP"); break;
    case Event::BRANCH: printf("BRANCH"); break;
    case Event::ADD: printf("+"); break;
    case Event::MUL: printf("*"); break;
    case Event::CMP_EQ: printf("=="); break;
    case Event::CMP_LT: printf("<"); break;
    case Event::CMP_LE: printf("<="); break;
    };
    printf(" {%x}", copySet);
    break;
  case Event::WALK_BACK:
    printf("Walk back to %d", offset + offsetToTarget);
    break;
  case Event::SKIP_BACK:
    printf("Skip back to %d", offset + offsetToTarget);
    break;
  case Event::PHI_LINK:
    printf("Phi Link [%d] to %d", phiLink.key, offset + phiLink.offsetToTarget);
    break;
  }
}

static const char* getRegName(unsigned registerSet) {
  static const char* regNames[] = { "EAX", "EDX", "EBX", "ECX", "ESP", "EBP", "ESI", "EDI", "R9", "R10", "R11", "R12", "R13", "R14", "R15" };
  //printf(">> %d : %d\n", registerSet, Try2::log2(registerSet));
  return regNames[Try2::log2(registerSet)];
}

static void printLiveRange(Event* event) {
  unsigned copySet = event->copySet;
  while (copySet) {
    unsigned copyReg = copySet & -copySet;
    printf("MOV %s %s", getRegName(copyReg), getRegName(event->liveRange.registerSet));
    copySet &= ~copyReg;
  }
}

static void printCommutable(Event* event, const char* name) {
  unsigned copySet = event->copySet;
  unsigned reg0 = event[-2].liveRange.registerSet;
  unsigned reg1 = event[-1].kind == Event::DUPLICATE_LIVE_RANGE ? event[-2].liveRange.registerSet : event[-1].liveRange.registerSet;
  if (!(reg0 & copySet) && (reg1 & copySet))
    std::swap(reg0, reg1);
  if (!(copySet & (reg0 | reg1))) {
    printf("MOV %s %s\n    ", getRegName(copySet & -copySet), getRegName(reg0));
    reg0 = copySet & -copySet;
  }
  copySet &= ~reg0;
  printf("%s %s %s", name, getRegName(reg0), getRegName(reg1));
  while (copySet) {
    unsigned copyReg = copySet & -copySet;
    printf("\n    MOV %s %s", getRegName(copyReg), getRegName(reg0));
    copySet &= ~copyReg;
  }
}

void Event::printASM(Event* events) {
  if (kind == Event::INT_LITERAL) {
    unsigned copySet = this->copySet;
    while (copySet) {
      unsigned copyReg = copySet & -copySet;
      printf("MOV %s %d", getRegName(copyReg), intLiteral);
      copySet &= ~copyReg;
    }
    return;
  }
  if (kind == Event::LIVE_RANGE) {
    unsigned copySet = this->copySet;
    copySet &= ~this->liveRange.registerSet;
    while (copySet) {
      unsigned copyReg = copySet & -copySet;
      printf("\n    MOV %s %s", getRegName(copyReg), getRegName(liveRange.registerSet));
      copySet &= ~copyReg;
    }
    return;
  }
  if (kind != Event::INSTRUCTION)
    return;
  switch (opcode) {
  case Event::JMP: printf("JMP"); break;
  case Event::BRANCH: printf("BRANCH"); break;
  case Event::ADD: printCommutable(this, "ADD"); break;
  case Event::MUL: printCommutable(this, "MUL"); break;
  case Event::CMP_EQ: printf("CMP"); break;
  case Event::CMP_LT: printf("CMP"); break;
  case Event::CMP_LE: printf("CMP"); break;
  };
}

struct RegisterAllocator {
  int getNewID() const { return (int)events.size(); }
  int getLastID() const { return (int)events.size() - 1; }

  explicit RegisterAllocator(SCFG* cfg);
  void emitTerminator(BasicBlock* basicBlock);
  void emitJump(BasicBlock* basicBlock, Goto* jump);
  void emitBranch(BasicBlock* basicBlock, Branch* branch);
  void emitLiteral(Literal* literal);
  void emitBinaryOp(BasicBlock* basicBlock, BinaryOp* binaryOp);
  void emitExpression(BasicBlock* basicBlock, SExpr* expr);
  void print();
  void printASM();

  void RegisterAllocator::determineCopy(Event* event, unsigned registerSet);
  void determinePressure(Event* event);
  unsigned determineClobberSet(Event* event, unsigned& sourceCopySet);
  void notifySelection(Event* event);

  std::vector<Event> events;
};

#include <conio.h>

RegisterAllocator::RegisterAllocator(SCFG* cfg) {
  BasicBlock* previousBlock = nullptr;
  // The header exists to live at location 0.  ID 0 is uses as uninitialized so nothing
  // may reference something at offset 0.
  events.push_back(Event().setHeader());
  for (auto block : *cfg) {
    printf("Block!!\n");
    int blockHeaderIndex = getNewID();
    if (block->DominatorNode.Parent) {
      int targetOffset = block->DominatorNode.Parent->VX64BlockEnd - blockHeaderIndex;
      if (targetOffset != -1) {
        if (block->PostDominates(*block->DominatorNode.Parent))
          events.push_back(Event().setWalkBack(targetOffset));
        else
          events.push_back(Event().setSkipBack(targetOffset));
      }
    }
    block->VX64BlockStart = blockHeaderIndex;
    for (auto arg : block->arguments()) {
      auto phi = cast<Phi>(arg->definition());
      int key = getNewID();
      for (auto value : phi->values())
        events.push_back(Event().setPhiLink(cast<Variable>(value)->id() - getNewID(), key));
      phi->setId(getLastID());
    }
    for (auto instr : block->instructions())
      emitExpression(block, instr);
    emitTerminator(block);
    block->VX64BlockEnd = getLastID();
    previousBlock = block;
    //return;
  }
  //return;
  // Compute pressure generated by the pairs.
  for (auto& event : events) {
    if (event.kind != Event::LIVE_RANGE)
      continue;
    determinePressure(&event);
  }
  std::vector<Event*> ptrs;
  for (auto& event : events)
    if (event.kind == Event::LIVE_RANGE)
      ptrs.push_back(&event);
  std::sort(ptrs.begin(), ptrs.end(), [](Event *a, Event *b) {return a->liveRange.pressure < b->liveRange.pressure;});
  for (auto ptr : ptrs) {
    unsigned sourceCopySet = 0;
    auto registerSet = ~determineClobberSet(ptr, sourceCopySet);
    unsigned destCopySet = ptr->copySet;
#if 1
    printf(">> %x %x %x", registerSet, sourceCopySet, destCopySet);
    if (registerSet & sourceCopySet & destCopySet) {
      registerSet &= sourceCopySet & destCopySet;
      printf(" <SD>");
    }
    else if (registerSet & destCopySet) {
      registerSet &= destCopySet;
      printf(" <D>");
    }
    else if (registerSet & sourceCopySet) {
      registerSet &= sourceCopySet;
      printf(" <S>");
    }
#endif
    printf(" %x : %x\n", registerSet, registerSet & -registerSet);
    ptr->liveRange.registerSet = registerSet & -registerSet;
    notifySelection(ptr);
  }
}

void RegisterAllocator::determinePressure(Event* event) {
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->liveRange.offsetToOrigin; i != e; --i) {
    if (i->kind == Event::WALK_BACK) {
      walkTo = std::min(walkTo, i + i->offsetToTarget);
      continue;
    }
    if (i->kind == Event::SKIP_BACK) {
      if (i + i->offsetToTarget < walkTo)
        i += i->offsetToTarget;
      continue;
    }
    if (i->kind == Event::LIVE_RANGE) {
      if (i + i->liveRange.offsetToOrigin == e)
        return;
      i->liveRange.pressure++;
    }
  }
}

unsigned RegisterAllocator::determineClobberSet(Event* event, unsigned& sourceCopySet) {
  unsigned clobberSet = event->liveRange.clobberedSet;
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->liveRange.offsetToOrigin; i != e; --i) {
    if (i->kind == Event::WALK_BACK) {
      walkTo = std::min(walkTo, i + i->offsetToTarget);
      continue;
    }
    if (i->kind == Event::SKIP_BACK) {
      if (i + i->offsetToTarget < walkTo)
        i += i->offsetToTarget;
      continue;
    }
    if (i->kind == Event::LIVE_RANGE) {
      if (i + i->liveRange.offsetToOrigin == e) {
        sourceCopySet = i->copySet;
        break;
      }
      clobberSet |= i->liveRange.registerSet;
    }
  }
  return clobberSet;
}

void RegisterAllocator::determineCopy(Event* event, unsigned registerSet) {
  switch (event->kind) {
  case Event::INSTRUCTION:
    event->copySet |= registerSet;
    break;
  case Event::INT_LITERAL:
    event->copySet |= registerSet;
    break;
  case Event::PHI_LINK:
    int key = event->phiLink.key;
    do {
      determineCopy(event + event->phiLink.offsetToTarget, registerSet);
      --event;
    } while (event->kind == Event::PHI_LINK && event->phiLink.key == key);
    break;
  }
}

void RegisterAllocator::notifySelection(Event* event) {
  unsigned registerSet = event->liveRange.registerSet;
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->liveRange.offsetToOrigin; i != e; --i) {
    if (i->kind == Event::WALK_BACK) {
      walkTo = std::min(walkTo, i + i->offsetToTarget);
      continue;
    }
    if (i->kind == Event::SKIP_BACK) {
      if (i + i->offsetToTarget < walkTo)
        i += i->offsetToTarget;
      continue;
    }
    if (i->kind == Event::LIVE_RANGE) {
      if (i + i->liveRange.offsetToOrigin == e) {
        i->copySet |= registerSet;
        return;
      }
      i->liveRange.clobberedSet |= registerSet;
    }
  }
  determineCopy(event + event->liveRange.offsetToOrigin, registerSet);
}

static const char* g_tabs[] = { "", "  ", "    ", "      ", "        ", "          " };
static int g_tab = 0;

void RegisterAllocator::emitLiteral(Literal* literal) {
  switch (literal->valueType().Base) {
  case ValueType::BT_Int:
    events.push_back(Event().setIntLiteral(literal->as<int>().value()));
    printf("%semitting int literal (%d) %p\n", g_tabs[g_tab], events.back().intLiteral, literal);
    break;
  default:
    assert(false);
  }
}

void RegisterAllocator::emitBinaryOp(BasicBlock* basicBlock, BinaryOp* binaryOp) {
  printf("%semitting binary op %p\n", g_tabs[g_tab], binaryOp);
  Event::OpCode opcode = Event::NOP;
  switch (binaryOp->binaryOpcode()) {
  case BOP_Add: opcode = Event::ADD; break;
  case BOP_Mul: opcode = Event::MUL; break;
  case BOP_Eq: opcode = Event::CMP_EQ; break;
  case BOP_Lt: opcode = Event::CMP_LT; break;
  case BOP_Leq: opcode = Event::CMP_LE; break;
  }
  SExpr* expr0 = binaryOp->expr0();
  SExpr* expr1 = binaryOp->expr1();
  emitExpression(basicBlock, expr0);
  emitExpression(basicBlock, expr1);
  int site = getNewID();
  events.push_back(Event().setLiveRange(expr0->id() - site));
  if (expr1 != expr0)
    events.push_back(Event().setLiveRange(expr1->id() - (site + 1)));
  else
    events.push_back(Event().setDuplicateLiveRange(-1));
  events.push_back(Event().setInstruction(opcode));
}

void RegisterAllocator::emitExpression(BasicBlock* basicBlock, SExpr* expr) {
  if (expr->id()) {
    printf("%salready emitted expression (%d) %p\n", g_tabs[g_tab], expr->id(), expr);
    return;
  }
  printf("%semitting expression (%d) %p\n", g_tabs[g_tab], getNewID(), expr);
  g_tab++;
  switch (expr->opcode()) {
  case COP_Literal: emitLiteral(cast<Literal>(expr)); break;
  case COP_Variable: emitExpression(basicBlock, cast<Variable>(expr)->definition()); break;
  case COP_BinaryOp: emitBinaryOp(basicBlock, cast<BinaryOp>(expr)); break;
  }
  g_tab--;
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
  else
    events.push_back(Event().setLiveRange(-1));
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
  for (auto arg : arguments)
    emitExpression(basicBlock, cast<Phi>(arg->definition())->values()[phiIndex]);
  for (auto arg : arguments)
    events.push_back(Event().setLiveRange(cast<Phi>(arg->definition())->values()[phiIndex]->id() - getNewID()));
  events.push_back(Event().setInstruction(Event::JMP));
}

void RegisterAllocator::emitBranch(BasicBlock* basicBlock, Branch* branch) {
  // There should be no critical edges.
  emitExpression(basicBlock, branch->condition());
  events.push_back(Event().setLiveRange(branch->condition()->id() - getNewID()));
  events.push_back(Event().setInstruction(Event::BRANCH));
}

void RegisterAllocator::print() {
  int i = 0;
  for (auto& event : events) {
    printf("%-2d: ", i++);
    event.print(events.data());
    printf("\n");
  }
}

void RegisterAllocator::printASM() {
  int i = 0;
  for (auto& event : events) {
    //if (event.kind != Event::INSTRUCTION && event.kind != Event::INT_LITERAL)
    //  continue;
    printf("%-2d: ", i++);
    event.printASM(events.data());
    printf("\n");
  }
}
} // namespace Try3

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
    //Try2::RegisterAllocator a(cfg);
    //a.print();
    Try3::RegisterAllocator a(cfg);
    a.print();
    a.printASM();
  }
  delete v;

  std::cout << "\n";
  return 0;
}

