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

// Literal hoisting:  In general, it's not necessary to allocate registers for
// literals.  They can use one

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

namespace Jagger {
Instruction countedMarker;
}  // namespace Jagger

namespace {
struct InstructionStream {
  void encode(SCFG* const* cfg, size_t numCFGs);

  static size_t countInstrs(SExpr* expr);
  Instruction* emitInstrs(Instruction* nextInstr, Block* block, SExpr* expr);
  Instruction* emitRet(Instruction* nextInstr, Block* block);
  //Instruction* emitInstrs(Instruction* nextInstr, SExpr* expr);
  //Instruction* emitInstrs(Instruction* nextInstr, Phi* phi);
  //Instruction* emitInstrs(Instruction* nextInstr, BasicBlock* basicBlock);
  //Instruction* emitInstrs(Instruction* nextInstr, Literal* literal);
  //Instruction* emitInstrs(Instruction* nextInstr, BinaryOp* binaryOp);
  //Instruction* emitInstrs(Instruction* nextInstr, BasicBlock* basicBlock, Goto* jump);
  //Instruction* emitInstrs(Instruction* nextInstr, BasicBlock* basicBlock, Branch* branch);

  Block* blocks;
  Instruction* instrs;
};
}  // namespace

void InstructionStream::encode(SCFG* const* cfgs, size_t numCFGs) {
  if (!numCFGs) return;

  // Count the blocks.d
  size_t numBlocks = 0;
  for (size_t i = 0; i < numCFGs; i++)
    numBlocks += cfgs[i]->numBlocks();
  assert(numBlocks);
  blocks = new Block[numBlocks];

  // Count the instructions.
  size_t numInstrs = 0;
  Block* nextBlock = blocks;
  for (size_t i = 0; i < numCFGs; i++)
    for (auto basicBlock : *cfgs[i]) {
      basicBlock->setBackendID(nextBlock);
      BasicBlock* parent = basicBlock->parent();
      // TODO: || parent is previous block
      while (parent && basicBlock->PostDominates(*parent))
        parent = parent->parent();
      nextBlock->parent = parent ? (Block*)parent->getBackendID() : nullptr;

      // ret instruction special case
      auto terminator = basicBlock->terminator();
      size_t size = terminator ? countInstrs(terminator) : 1;
      for (auto arg : basicBlock->arguments()) size += countInstrs(arg);
      for (auto instr : basicBlock->instructions()) size += countInstrs(instr);
      nextBlock++->numInstrs = size;
      numInstrs += size;
    }
  assert(numInstrs);
  instrs = new Instruction[numInstrs];

#if 0
  Instruction* nextInstr = instrs;
  nextBlock = blocks;
  for (size_t i = 0; i < numCFGs; i++)
    for (auto basicBlock : *cfgs[i]) {
      nextBlock->instrs = nextInstr;
      for (auto arg : basicBlock->arguments())
        nextInstr = emitInstrs(nextInstr, nextBlock, arg);
      for (auto instr : basicBlock->instructions())
        nextInstr = emitInstrs(nextInstr, nextBlock, instr);
      auto terminator = basicBlock->terminator();
      nextInstr = terminator ? emitInstrs(nextInstr, nextBlock, terminator)
                             : emitRet(nextInstr, nextBlock);
      assert(nextInstr == nextBlock->instrs + nextBlock->numInstrs);
      nextBlock++;
    }
#endif

  printf("blocks = %d\ninstrs = %d\n", (int)numBlocks, (int)numInstrs);
  for (size_t i = 0; i < numBlocks; i++)
    printf("block%d\n  parent = %d\n  first = %d\n  instrs = %d\n", (int)i,
           blocks[i].parent ? (int)(blocks[i].parent - blocks) : -1,
           (int)(blocks[i].instrs - instrs),
           (int)blocks[i].numInstrs);

#if 0
  size_t numInstrs = 0;
  for (auto block : *cfg) {
    numBlocks++;
    numInstrs += countInstrs(block);
  }
  printf("blocks = %d\ninstrs = %d\n", (int)numBlocks, (int)numInstrs);
  size_t size = sizeof(Opcodes) + sizeof(Procedure) * numProcs +
                sizeof(Block) * numBlocks + sizeof(Instruction) * numInstrs;
  assert(size <= 0x8000000u);
  buffer = (char*)malloc(size);
  opcodes = (Opcodes*)buffer;
  firstProc = nextProc = (Procedure*)(buffer + sizeof(Opcodes));
  firstBlock = nextBlock =
      (Block*)(buffer + sizeof(Opcodes) + sizeof(Procedure) * numProcs);
  firstInstr = nextInstr =
      (Instruction*)(buffer + sizeof(Opcodes) + sizeof(Procedure) * numProcs +
                     sizeof(Instruction) * numInstrs);
  memcpy(opcodes, &globalOpcodes, sizeof(Opcodes));

  for (auto block : *cfg)
    emitBlock(block);
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
#endif
}

size_t InstructionStream::countInstrs(SExpr* expr) {
  if (expr->id()) return 0;
  expr->setBackendID(&countedMarker);
  switch (expr->opcode()) {
    case COP_Literal:
      return 1;
    case COP_VarDecl:
      return countInstrs(cast<VarDecl>(expr)->definition());
    case COP_BinaryOp:
      return countInstrs(cast<BinaryOp>(expr)->expr0()) +
             countInstrs(cast<BinaryOp>(expr)->expr1()) + 1;
    case COP_Phi:
      // Because phi instructions are binary we need n-1 of them to make a tree
      // of phi/tie instructions with n leaves.
      return cast<Phi>(expr)->values().size() - 1;
    case COP_Goto:
      return cast<Goto>(expr)->targetBlock()->arguments().size() + 1;
    case COP_Branch:
      return countInstrs(cast<Branch>(expr)->condition()) + 1;
    default:
      printf("unknown opcode: %d\n", expr->opcode());
      assert(false);
      return 0;
  }
}

Instruction* InstructionStream::emitInstrs(Instruction* nextInstr, Block* block,
                                           SExpr* expr) {
  if (expr->getBackendID() != &countedMarker) return (Instruction*)expr->id();
  expr->setBackendID(nextInstr);
  switch (expr->opcode()) {
  case COP_Literal: {
    Literal* literal = cast<Literal>(expr);
    switch (literal->valueType().Base) {
    case ValueType::BT_Int:
      *nextInstr = Instruction(block, &globalOpcodes.intValue,
        (Instruction*)literal->as<int>().value());
      break;
    default:
      assert(false);
    }
  }
#if 0
  case COP_VarDecl:
    return countInstrs(cast<VarDecl>(expr)->definition());
  case COP_BinaryOp:
    return countInstrs(cast<BinaryOp>(expr)->expr0()) +
      countInstrs(cast<BinaryOp>(expr)->expr1()) + 1;
  case COP_Phi:
    // Because phi instructions are binary we need n-1 of them to make a tree
    // of phi/tie instructions with n leaves.
    return cast<Phi>(expr)->values().size() - 1;
  case COP_Goto:
    return cast<Goto>(expr)->targetBlock()->arguments().size() + 1;
  case COP_Branch:
    return countInstrs(cast<Branch>(expr)->condition()) + 1;
#endif
  default:
    printf("unknown opcode: %d\n", expr->opcode());
    assert(false);
    return 0;
  }
  return nextInstr;
}

#if 0
void InstructionStream::emitPhi(Phi* phi) {
  int id = getNewID();
  if (phi->values().size() == 1) {
    // TODO: eliminate this case in the middle-end
    phi->setId((phi->values()[0])->id());
    return;
  }
  assert(phi->values().size() == 2);
  instrs.push_back(Instruction(
    currentBlock,
    &OpCodes::phi,
    cast<VarDecl>(phi->values()[0])->id() - id,
    cast<VarDecl>(phi->values()[1])->id() - id));
  phi->setId(id);
}

void InstructionStream::emitLiteral(Literal* literal) {
  switch (literal->valueType().Base) {
  case ValueType::BT_Int:
    instrs.push_back(Instruction(
      currentBlock,
      &OpCodes::intValue,
      literal->as<int>().value()));
    break;
  default:
    assert(false);
  }
}

void InstructionStream::emitBinaryOp(BinaryOp* binaryOp) {
  int expr0ID = emitExpression(binaryOp->expr0());
  int expr1ID = emitExpression(binaryOp->expr1());
  const OpCode* opcode = &OpCodes::nop;
  switch (binaryOp->binaryOpcode()) {
  case BOP_Add: opcode = &OpCodes::add; break;
  case BOP_Mul: opcode = &OpCodes::mul; break;
  case BOP_Eq: opcode = &OpCodes::cmpeq; break;
  case BOP_Lt: opcode = &OpCodes::cmplt; break;
  case BOP_Leq: opcode = &OpCodes::cmple; break;
  default:
    assert(false);
  }
  int site = getNewID();
  instrs.push_back(Instruction(currentBlock, opcode, expr0ID - site, expr1ID - site));
}

int InstructionStream::emitExpression(SExpr* expr) {
  if (expr->id())
    return expr->id();
  switch (expr->opcode()) {
  case COP_Literal: emitLiteral(cast<Literal>(expr)); break;
  case COP_VarDecl: emitExpression(cast<VarDecl>(expr)->definition()); break;
  case COP_BinaryOp: emitBinaryOp(cast<BinaryOp>(expr)); break;
  }
  expr->setId(getLastID());
  return expr->id();
}

void InstructionStream::emitTerminator(BasicBlock* basicBlock) {
  auto expr = basicBlock->terminator();
  if (!expr) {
    // Presently Ohmu IR doesn't have/use ret instructions.
    // We should figure out how to differentiate functions that return values
    // and those that don't.
    //assert(!instrs.empty() && instrs.back().opcode->hasResult);
    //assert(basicBlock->instructions().size() != 0);
    instrs.push_back(Instruction(currentBlock, &OpCodes::ret, -1));
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
  for (auto arg : arguments) {
    SExpr* expr = cast<Phi>(arg->definition())->values()[phiIndex];
    int argid = emitExpression(expr);
    int echoid = getNewID();
    instrs.push_back(Instruction(currentBlock, &OpCodes::echo, argid - echoid));
    expr->setId(echoid);
  }
  instrs.push_back(Instruction(currentBlock, &OpCodes::jump));
}

void InstructionStream::emitBranch(BasicBlock* basicBlock, Branch* branch) {
  // There should be no critical edges.
  emitExpression(branch->condition());
  instrs.push_back(Instruction(currentBlock, &OpCodes::branch,
    branch->condition()->id() - getNewID()));
}
#endif










#if 0

#if 0
namespace {
struct Pool {
  Pool() : data((char*)malloc(0x100)), capacity(0x100), size(0) {}

  template <typename T>
  T* allocate(size_t quantity) {
    assert(!(sizeof(T) % 4) && __alignof(T) <= 4);
    size_t offset = size;
    if (quantity) {
      size += sizeof(T) * quantity;
      while (size > capacity) data = (char*)realloc(data, capacity *= 2);
    }
    return (T*)(data + offset);
  }

  size_t getSize() const { return size; }
  int getOffset() const { assert((int)size == size); return (int)size; }
  char* getBase() const { return data; }

 private:
  char* data;
  size_t capacity;
  size_t size;
};
#endif

struct InstructionStream {
#if 0
  int getNewInstr() const { return pool.getOffset(); }
  int getLastInstr() const { return getNewInstr() - sizeof(Instruction); }
  int getNewBlock() const { return pool.getOffset(); }
  int getLastBlock() const { return getNewBlock() - sizeof(Block); }
#endif

  int countBytes(SCFG* cfg) const;
  int countInstrs(SExpr* expr) const;
  int countInstrs(BasicBlock* block) const;

#if 0
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

  void printWalk(Instruction* instr, Instruction* target);
  void printWalks();
#endif

#if 0
  void computePressure(Event* event);
  unsigned computeValidRegs(Event* event, Event*& source);
  void markInvalidRegs(Event* event);
  void propegateCopies(Event* source, unsigned reg);
  void untwistPairs(Event* event);
#endif

private:
  //Block* currentBlock;
  //Pool pool;
  //void* root;
  void* root;
};
} // namespace

int InstructionStream::countBytes(SCFG* cfg) const {
  int numBlocks = 0;
  int numInstrs = 0;
  for (auto block : *cfg) {
    numBlocks++;
    numInstrs += countInstrs(block);
  }
  printf("blocks = %d\ninstrs = %d\n", numBlocks, numInstrs);
  return numBlocks * (int)sizeof(Block) + numInstrs * (int)sizeof(Instruction);
}

int InstructionStream::countInstrs(BasicBlock* block) const {
  int x = 0;
  // count the number of phi instructions.  Because phi instructions are binary
  // we need n-1 of them to make a tree of phi/tie instructions with n leaves.
  x += block->arguments().size() * (block->predecessors().size() - 1);
  for (auto instr : block->instructions()) x += countInstrs(instr);
  auto terminator = block->terminator();
  if (!terminator)
    x++;  // ret instruction special case
  else
    x += countInstrs(terminator);
  return x;
}

int InstructionStream::countInstrs(SExpr* expr) const {
  if (expr->id()) return 0;
  expr->setId(1);  // all ids < 4 are special
  switch (expr->opcode()) {
    case COP_Literal:
      return 1;
    case COP_VarDecl:
      return countInstrs(cast<VarDecl>(expr)->definition());
    case COP_BinaryOp:
      return countInstrs(cast<BinaryOp>(expr)->expr0()) +
             countInstrs(cast<BinaryOp>(expr)->expr1()) + 1;
    case COP_Goto:
      return cast<Goto>(expr)->targetBlock()->arguments().size() + 1;
    case COP_Branch:
      return countInstrs(cast<Branch>(expr)->condition()) + 1;
    default:
      assert(false);
      return 0;
  }
}

void InstructionStream::emitBlock(BasicBlock* block) {
  int blockID = block->blockID();
  assert(blockID == (int)blocks.size());
  BasicBlock* parent = block->DominatorNode.Parent;
  while (parent && block->PostDominates(*parent)) // TODO: || parent is previous block
    parent = parent->DominatorNode.Parent;
  blocks.push_back(Block(parent ? parent->blockID() - blockID : 0, getNewID()));
  currentBlock = &blocks.back();
  for (auto arg : block->arguments())
    emitPhi(cast<clang::threadSafety::til::Phi>(arg->definition()));
  for (auto instr : block->instructions())
    emitExpression(instr);
  emitTerminator(block);
  currentBlock->lastInstr = getLastID();
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
  int id = getNewID();
  if (phi->values().size() == 1) {
    // TODO: eliminate this case in the middle-end
    phi->setId((phi->values()[0])->id());
    return;
  }
  assert(phi->values().size() == 2);
  instrs.push_back(Instruction(
      currentBlock,
      &OpCodes::phi,
      cast<VarDecl>(phi->values()[0])->id() - id,
      cast<VarDecl>(phi->values()[1])->id() - id));
  phi->setId(id);
}

void InstructionStream::emitLiteral(Literal* literal) {
  switch (literal->valueType().Base) {
  case ValueType::BT_Int:
    instrs.push_back(Instruction(
      currentBlock,
      &OpCodes::intValue,
      literal->as<int>().value()));
    break;
  default:
    assert(false);
  }
}

void InstructionStream::emitBinaryOp(BinaryOp* binaryOp) {
  int expr0ID = emitExpression(binaryOp->expr0());
  int expr1ID = emitExpression(binaryOp->expr1());
  const OpCode* opcode = &OpCodes::nop;
  switch (binaryOp->binaryOpcode()) {
  case BOP_Add: opcode = &OpCodes::add; break;
  case BOP_Mul: opcode = &OpCodes::mul; break;
  case BOP_Eq : opcode = &OpCodes::cmpeq; break;
  case BOP_Lt : opcode = &OpCodes::cmplt; break;
  case BOP_Leq: opcode = &OpCodes::cmple; break;
  default:
    assert(false);
  }
  int site = getNewID();
  instrs.push_back(Instruction(currentBlock, opcode, expr0ID - site, expr1ID - site));
}

int InstructionStream::emitExpression(SExpr* expr) {
  if (expr->id())
    return expr->id();
  switch (expr->opcode()) {
  case COP_Literal: emitLiteral(cast<Literal>(expr)); break;
  case COP_VarDecl: emitExpression(cast<VarDecl>(expr)->definition()); break;
  case COP_BinaryOp: emitBinaryOp(cast<BinaryOp>(expr)); break;
  }
  expr->setId(getLastID());
  return expr->id();
}

void InstructionStream::emitTerminator(BasicBlock* basicBlock) {
  auto expr = basicBlock->terminator();
  if (!expr) {
    // Presently Ohmu IR doesn't have/use ret instructions.
    // We should figure out how to differentiate functions that return values
    // and those that don't.
    //assert(!instrs.empty() && instrs.back().opcode->hasResult);
    //assert(basicBlock->instructions().size() != 0);
    instrs.push_back(Instruction(currentBlock, &OpCodes::ret, -1));
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
  for (auto arg : arguments) {
    SExpr* expr = cast<Phi>(arg->definition())->values()[phiIndex];
    int argid = emitExpression(expr);
    int echoid = getNewID();
    instrs.push_back(Instruction(currentBlock, &OpCodes::echo, argid - echoid));
    expr->setId(echoid);
  }
  instrs.push_back(Instruction(currentBlock, &OpCodes::jump));
}

void InstructionStream::emitBranch(BasicBlock* basicBlock, Branch* branch) {
  // There should be no critical edges.
  emitExpression(branch->condition());
  instrs.push_back(Instruction(currentBlock, &OpCodes::branch,
                               branch->condition()->id() - getNewID()));
}

void InstructionStream::encode(SCFG* cfg) {
  for (auto block : *cfg)
    emitBlock(block);
  instrs.push_back(Instruction(currentBlock, &OpCodes::nop));
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

void InstructionStream::printWalk(Instruction* instr, Instruction* target) {
  Instruction* first = instrs.data() + instr->block->firstInstr;
}

void InstructionStream::printWalks() {
  //return;
  for (size_t i = 0, e = instrs.size(); i != e; ++i) {
    Instruction* instr = &instrs[i];
    if (!instr->opcode->hasArg0)
      continue;
    Instruction* first = instrs.data() + instr->block->firstInstr;
    //printf("first = %d\n", first - instrs.data());
    if (instr->opcode->hasArg0) {
      Instruction* target = instr + instr->arg0;
      printf("? %d => %d\n", i, i + instr->arg0);
      for (;;) {
        for (; instr != target && instr >= first; instr--)
          printf("%d->", instr - instrs.data());
        if (instr == target)
          break;
        //break;
        const Block* block = instr->block;
        assert(instrs.data() + block->firstInstr == instr + 1);
        if (!block->parent)
          break;
        //printf("block : %d parent : %d current : %d next : %d target : %d\n",
        //  block - blocks.data(), );
        instr += (block + block->parent)->lastInstr - block->firstInstr - 1;
      }
      printf("%d\n", instr - instrs.data());
      instr = &instrs[i];
    }
#if 0
    if (instr->opcode->hasArg1) {
      Instruction* target = instr + instr->arg1;
      printf("? %d => %d\n", i, i + instr->arg1);
      for (;;) {
        for (; instr != target && instr >= first; instr--)
          printf("%d->", instr - instrs.data());
        if (instr == target)
          break;
        //break;
        const Block* block = instr->block;
        assert(instrs.data() + block->firstInstr == instr + 1);
        if (!block->parent)
          break;
        //printf("block : %d parent : %d current : %d next : %d target : %d\n",
        //  block - blocks.data(), );
        instr += (block + block->parent)->lastInstr - block->firstInstr - 1;
      }
      printf("%d\n", instr - instrs.data());
    }
#endif
  }
}

// FIXME put me in a header
//extern void emitASM(X64Builder& builder, Event* events, size_t numEvents);
#endif

void encode(SCFG* cfg, char* output) {
  InstructionStream stream;
  //stream.encode(&cfg, 1);

  //print(stream.instrs.data(), stream.instrs.size());
  //stream.printWalks();
  //X64Builder builder;
  //emitASM(builder, InstructionStream.events.data(), InstructionStream.events.size());
}
