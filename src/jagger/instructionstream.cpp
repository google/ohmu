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

// Note: approach for handling pressure that exceeds the number of bits we have
// in our "registerSet" bitfields:  Given that we use a bitmask to store the
// remaining valid registers for a given live range, if that set becomes all 0
// we cannot allocate a register for this register and a "spill" must occur.  I
// use "spill" in quotes because we may have more registers than we do bits.
// Once we've completed a pass of register allocation we will have allocated
// registers up to the bitwidth of our registerSet bit set.  Everything that
// couldn't have been allocated will have a 0 in their register field.  We can
// run another pass to allocate these independantly, ignoring the already
// allocated liveRanges.  These passes can continue to allocate registers if
// registers exist or they can allocate spill slots.  After some number of
// finite passes, the allocator can assign fixed slots to the remaining live
// ranges.

// Copying at an instruction:  Sometimes it's necessary to copy arguments (e.g.
// because they're destroyed by the instruction but still live) and results
// (because they are used multiple times).  Sometimes they instruction will need
// additional registers to perform these copies e.g. c = (a - b) + a;  The sub
// will destroy a, but the add needs it.  However, if the register allocator
// allocates a and b to the same registers as the continuing a and c then we
// must destroy the register that contains b.  However the semantics of x86 say
// we must destroy the register contining a.  Given that we can only see two
// registers and we need 3 (storage for a, result (c) and argument b) we must
// allocate a register or spill slot.  This occurs after general register
// allocation and require us knowing which registers are free at point in which
// the instruction executes.

// Live ranges:  Each instruction result (SSA value) can have multiple live
// ranges.  These live ranges can, in general, overlap.  Overlapping must occur
// at a point when a destructive operation consumes a value in the middle of its
// live range.  

// Live range splitting/merging: Using the model that each use corresponds to a live
// range leads to an inefficiency if we use the same argument twice for an
// instruction.  Each use constitutes a 'different' live range even though it
// shouldn't.

// Literal hoisting:  In general, it's not necessary to allocate registers for literals.  They can use one 

#include "types.h"
#include "interface.h"
#include <vector>

using namespace clang::threadSafety::til;
using namespace Jagger;

#ifdef _MSC_VER
extern "C" unsigned char _BitScanForward(unsigned long * Index, unsigned long Mask);
#endif

static int lowIndex(unsigned x) {
  unsigned long a;
  _BitScanReverse(&a, (unsigned long)x);
  return (int)a;
}

namespace {
struct InstructionStream {
  int getNewID() const { return (int)instrs.size(); }
  int getLastID() const { return (int)instrs.size() - 1; }

  void emitBlock(BasicBlock* block);
  //void emitBlockLink(BasicBlock* block);
  void emitEchos(Phi* phi);
  void emitPhi(Phi* phi);
  void encode(SCFG* cfg);

  int emitExpression(SExpr* expr);
  void emitTerminator(BasicBlock* basicBlock);

  void emitLiteral(Literal* literal);
  void emitBinaryOp(BinaryOp* binaryOp);
  void emitJump(BasicBlock* basicBlock, Goto* jump);
  void emitBranch(BasicBlock* basicBlock, Branch* branch);

#if 0
  void computePressure(Event* event);
  unsigned computeValidRegs(Event* event, Event*& source);
  void markInvalidRegs(Event* event);
  void propegateCopies(Event* source, unsigned reg);
  void untwistPairs(Event* event);
#endif

  std::vector<Block*> blocks;
  std::vector<Instruction> instrs;
};
} // namespace

void InstructionStream::emitBlock(BasicBlock* block) {
  block->VX64BlockStart = getNewID();
  //emitBlockLink(block);
  for (auto arg : block->arguments())
    emitPhi(cast<clang::threadSafety::til::Phi>(arg->definition()));
  for (auto instr : block->instructions())
    emitExpression(instr);
  emitTerminator(block);
  block->VX64BlockEnd = getLastID();
}

#if 0
void InstructionStream::emitBlockLink(BasicBlock* block) {
  if (!block->DominatorNode.Parent)
    return;
  int targetOffset = block->DominatorNode.Parent->VX64BlockEnd - getNewID();
  // In this case the previous block is immediately prior and we don't need to
  // walk or skip.
  if (targetOffset == -1)
    return;
  if (block->PostDominates(*block->DominatorNode.Parent))
    events.push_back(Link().initWalkBack(targetOffset));
  else
    events.push_back(Link().initSkipBack(targetOffset));
}
#endif

void InstructionStream::emitPhi(Phi* phi) {
  assert(phi->values().size() == 2);
  int id = getNewID();
  instrs.push_back(Instruction().init(
      Instruction::PHI,
      cast<Variable>(phi->values()[0])->id() - id,
      cast<Variable>(phi->values()[1])->id() - id));
  phi->setId(id);
}

void InstructionStream::emitLiteral(Literal* literal) {
  switch (literal->valueType().Base) {
  case ValueType::BT_Int:
    instrs.push_back(Instruction().init(literal->as<int>().value()));
    break;
  default:
    assert(false);
  }
}

void InstructionStream::emitBinaryOp(BinaryOp* binaryOp) {
  int expr0ID = emitExpression(binaryOp->expr0());
  int expr1ID = emitExpression(binaryOp->expr1());
  Instruction::OpCode opcode = Instruction::NOP;
  switch (binaryOp->binaryOpcode()) {
  case BOP_Add: opcode = Instruction::ADD; break;
  case BOP_Mul: opcode = Instruction::MUL; break;
  case BOP_Eq : opcode = Instruction::CMP_EQ; break;
  case BOP_Lt : opcode = Instruction::CMP_LT; break;
  case BOP_Leq: opcode = Instruction::CMP_LE; break;
  default:
    assert(false);
  }
  int site = getNewID();
  instrs.push_back(Instruction().init(opcode, expr0ID - site, expr1ID - site));
}

int InstructionStream::emitExpression(SExpr* expr) {
  if (expr->id())
    return expr->id();
  switch (expr->opcode()) {
  case COP_Literal: emitLiteral(cast<Literal>(expr)); break;
  case COP_Variable: emitExpression(cast<Variable>(expr)->definition()); break;
  case COP_BinaryOp: emitBinaryOp(cast<BinaryOp>(expr)); break;
  }
  expr->setId(getLastID());
  return expr->id();
}

void InstructionStream::emitTerminator(BasicBlock* basicBlock) {
  auto expr = basicBlock->terminator();
  if (!expr) {
    // Presently Ohmu IR doesn't have/use ret instructions.
    instrs.push_back(Instruction().init(Instruction::RET, -1));
    return;
  }
  switch (expr->opcode()) {
  case COP_Goto: emitJump(basicBlock, cast<Goto>(expr)); break;
  case COP_Branch: emitBranch(basicBlock, cast<Branch>(expr)); break;
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

void InstructionStream::emitJump(BasicBlock* basicBlock, Goto* jump) {
  auto targetBlock = jump->targetBlock();
  size_t phiIndex = getPhiIndex(basicBlock, targetBlock);
  auto& arguments = targetBlock->arguments();
  int site = getNewID();
  for (auto arg : arguments) {
    int argid = emitExpression(cast<Phi>(arg->definition())->values()[phiIndex]);
    instrs.push_back(Instruction().init(Instruction::ECHO, argid - site));
  }
  instrs.push_back(Instruction().init(Instruction::JUMP));
}

void InstructionStream::emitBranch(BasicBlock* basicBlock, Branch* branch) {
  // There should be no critical edges.
  emitExpression(branch->condition());
  instrs.push_back(Instruction().init(Instruction::BRANCH,
                                      branch->condition()->id() - getNewID(),
                                      0));
}

void InstructionStream::encode(SCFG* cfg) {
  instrs.push_back(Instruction().init(Instruction::NOP));
  for (auto block : *cfg) {
    Block* b = new Block();
    // TODO:b.init();
    emitBlock(block);
  }
  // Patch up all of the jump targets.
  for (auto block : *cfg) {
    if (!block->terminator())
      continue;
    switch (block->terminator()->opcode()) {
    case COP_Goto:
      instrs[block->VX64BlockEnd].arg1 =
          cast<Goto>(block->terminator())->targetBlock()->VX64BlockStart - block->VX64BlockEnd;
      break;
    case COP_Branch:
      instrs[block->VX64BlockEnd].arg1 =
          cast<Branch>(block->terminator())->elseBlock()->VX64BlockStart - block->VX64BlockEnd;
      break;
    }
  }
#if 0
  // Compute pressure generated by the pairs.
  for (auto& event : instrs) {
    if (event.kind != Object::USE)
      continue;
    computePressure(&event);
  }
  std::vector<Event*> ptrs;
  for (auto& event : instrs)
    if (event.kind == Object::USE)
      ptrs.push_back(&event);
  std::sort(ptrs.begin(), ptrs.end(), [](Event *a, Event *b) {
    a = a + a->use.offsetToValue;
    b = b + b->use.offsetToValue;
    return a->liveRange.pressure < b->liveRange.pressure;
  });
#endif
#if 0
  for (auto ptr : ptrs) {
    Event* source = nullptr;
    auto registerSet = computeValidRegs(ptr, source);
    unsigned sourceCopySet = source->value.copySet;
    unsigned destCopySet = ptr->value.copySet;
// FIXME if we have c = a - b, a must survive, and the total number of allocated
// registers for c, b, and a is 2 then we have no place to squirl away a during
// computation of c so we need to add an artifical use of a so that we get
// another register allocated as a place to store it.
#if 1
    // printf(">> %x %x %x", registerSet, sourceCopySet, destCopySet);
    if (registerSet & sourceCopySet & destCopySet) {
      registerSet &= sourceCopySet & destCopySet;
      //printf(" <SD>");
    }
    else if (registerSet & destCopySet) {
      registerSet &= destCopySet;
      //printf(" <D>");
    }
    else if (registerSet & sourceCopySet) {
      registerSet &= sourceCopySet;
      //printf(" <S>");
    }
#endif
    unsigned reg = registerSet & -registerSet;
    //printf(" %x : %x\n", registerSet, reg);
    ptr->liveRange.reg = reg;
    markInvalidRegs(ptr);
    propegateCopies(source, reg);
  }
#endif
}

#if 0
void InstructionStream::computePressure(Event* event) {
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->use.offsetToValue; i != e; --i)
    switch (i->kind) {
    case Object::HOLE: break;
    case Object::WALK_BACK:
      walkTo = std::min(walkTo, i + i->link.offsetToTarget);
      break;
    case Object::SKIP_BACK:
      if (i + i->link.offsetToTarget < walkTo)
        i += i->link.offsetToTarget;
      break;
    case Object::USE:
      // TODO: this will double count some values
      if (i < walkTo && i + i->use.offsetToValue == e)
        return;
      break;
    case Object::JUMP:
    case Object::BRANCH: break;
    default:
      assert(i->value.isValue());
      assert((i + i->value.offsetToRep)->value.isValue());
      (i + i->value.offsetToRep)->value.pressure++;
  }
}
#endif

#if 0
unsigned InstructionStream::computeValidRegs(Event* event, Event*& source) {
  unsigned validSet = ~event->liveRange.invalidRegs;
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->liveRange.offsetToOrigin; i != e; --i)
    switch (i->kind) {
    case Object::WALK_BACK:
      walkTo = std::min(walkTo, i + i->link.offsetToTarget);
      break;
    case Object::SKIP_BACK:
      if (i + i->link.offsetToTarget < walkTo)
        i += i->link.offsetToTarget;
      break;
    case Object::LIVE_RANGE:
      if (i < walkTo && i + i->liveRange.offsetToOrigin == e) {
        source = i;
        return validSet;
      }
      validSet &= ~(i->liveRange.reg | i->value.copySet | i->liveRange.invalidRegs);
    }
  source = event + event->liveRange.offsetToOrigin;
  return validSet;
}
#endif

#if 0
void InstructionStream::markInvalidRegs(Event* event) {
  unsigned reg = event->liveRange.reg;
  Event* walkTo = event;
  for (Event* i = event - 1, *e = event + event->liveRange.offsetToOrigin; i != e; --i)
    switch (i->kind) {
    case Object::WALK_BACK:
      walkTo = std::min(walkTo, i + i->link.offsetToTarget);
      break;
    case Object::SKIP_BACK:
      if (i + i->link.offsetToTarget < walkTo)
        i += i->link.offsetToTarget;
      break;
    case Object::LIVE_RANGE:
      if (i < walkTo && i + i->liveRange.offsetToOrigin == e)
        return;
      i->liveRange.invalidRegs |= reg;
    }
}
#endif

#if 0
void InstructionStream::propegateCopies(Event* source, unsigned reg) {
  if (source->kind == Object::PHI) {
    int key = source->phiLink.key;
    do {
      propegateCopies(source + source->phiLink.offsetToTarget, reg);
      --source;
    } while (source->kind == Object::PHI && source->phiLink.key == key);
    return;
  }
  assert(source->object.isValue());
  source->value.copySet |= reg;
}
#endif

#if 0
void InstructionStream::untwistPairs(Event* event) {
  if (event->kind != Object::ADD)
    return;
  unsigned copySet = event->instruction.copySet;
  unsigned reg0 = event[-2].liveRange.reg;
  unsigned reg1 = event[-1].liveRange.reg;
  if (!(reg0 & copySet) && (reg1 & copySet))
    std::swap(event[-2].liveRange.reg, event[-1].liveRange.reg);
}
#endif

// FIXME put me in a header
//extern void emitASM(X64Builder& builder, Event* events, size_t numEvents);

void encode(SCFG* cfg, char* output) {
  InstructionStream stream;
  stream.encode(cfg);
  print(stream.instrs.data());
  //X64Builder builder;
  //emitASM(builder, InstructionStream.events.data(), InstructionStream.events.size());
}
