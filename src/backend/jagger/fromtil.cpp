#include <cassert>
#include "types.h"
#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "til/Global.h"
#include "til/VisitCFG.h"
#include "util.h"


using namespace clang::threadSafety::til;
typedef unsigned int uint;

size_t Event::initNop(Event* events, size_t i, uint payload) {
  if (events) {
    events[i + 0] = Event(NOP, payload);
  }
  return i + 1;
}

size_t Event::initGotoHeader(Event* events, size_t i, uint target) {
  if (events) {
    events[i + 0] = Event(GOTO_HEADER, target);
  }
  return i + 1;
}

size_t Event::initWalkHeader(Event* events, size_t i, uint target) {
  if (!events) return i + 1;
  events[i + 0] = Event(WALK_HEADER, target);
  return i + 1;
}

size_t Event::initPhi(Event* events, size_t i) {
  if (!events) return i + 1;
  events[i + 0] = Event(PHI | GPR, i + 0);
}

size_t Event::initPhiCopy(Event* events, size_t i, uint arg0, uint phi) {
  if (!events) return i + 2;
  events[i + 0] = Event(PHI_COPY | GPR, phi);
  events[i + 1] = Event(USE, arg0);
  return i + 2;
}

size_t Event::initJump(Event* events, size_t i, uint target) {
  if (!events) return i + 1;
  events[i + 0] = Event(JUMP, target);
  return i + 1;
}

size_t Event::initBranch(Event* events, size_t i, uint arg0, uint thenTarget, uint elseTarget) {
  if (!events) return i + 3;
  events[i + 0] = Event(BRANCH, thenTarget);
  events[i + 1] = Event(NOP, elseTarget);
  events[i + 2] = Event(USE, arg0);
  return i + 3;
}

size_t Event::initRet(Event* events, size_t i, uint arg0) {
  if (!events) return i + 3;
  events[i + 0] = Event(RET, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(FIXED | GPR, arg0);
  return i + 3;
}

size_t Event::initIntLiteral(Event* events, size_t i, int value) {
  if (!events) return i + 2;
  events[i + 0] = Event(INT32, 0);
  events[i + 1] = Event(VALUE | GPR, (unsigned)value);
  return i + 2;
}

size_t Event::initAdd(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 5;
  events[i + 0] = Event(ADD, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(COPY | GPR, i + 3);
  events[i + 4] = Event(FIXED | FLAGS, i + 4);
  return i + 5;
}

size_t Event::initSub(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 5;
  events[i + 0] = Event(SUB, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(COPY | GPR, i + 2);
  events[i + 3] = Event(USE, arg1);
  events[i + 4] = Event(FIXED | FLAGS, i + 4);
  return i + 5;
}

size_t Event::initMul(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 9;
  events[i + 0] = Event(MUL, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(FIXED | EAX, arg0);
  events[i + 4] = Event(FIXED | EAX, arg1);
  events[i + 5] = Event(FIXED | EDX, i + 5);
  events[i + 6] = Event(FIXED | EAX, i + 7);
  events[i + 7] = Event(COPY | GPR, i + 7);
  events[i + 8] = Event(FIXED | FLAGS, i + 8);
  return i + 9;
}

size_t Event::initEq(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 4;
  events[i + 0] = Event(SUB, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}

size_t Event::initLt(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 4;
  events[i + 0] = Event(SUB, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}

size_t Event::initLe(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 4;
  events[i + 0] = Event(SUB, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}

#if 0
TIL_OPCODE_DEF(Literal)
TIL_OPCODE_DEF(LiteralPtr)
TIL_OPCODE_DEF(Variable)
TIL_OPCODE_DEF(Function)
TIL_OPCODE_DEF(SFunction)
TIL_OPCODE_DEF(Code)
TIL_OPCODE_DEF(Field)

TIL_OPCODE_DEF(Apply)
TIL_OPCODE_DEF(SApply)
TIL_OPCODE_DEF(Project)

TIL_OPCODE_DEF(Call)
TIL_OPCODE_DEF(Alloc)
TIL_OPCODE_DEF(Load)
TIL_OPCODE_DEF(Store)
TIL_OPCODE_DEF(ArrayIndex)
TIL_OPCODE_DEF(ArrayAdd)

TIL_OPCODE_DEF(UnaryOp)
TIL_OPCODE_DEF(BinaryOp)
TIL_OPCODE_DEF(Cast)

TIL_OPCODE_DEF(SCFG)
TIL_OPCODE_DEF(BasicBlock)
TIL_OPCODE_DEF(Phi)

// Terminator instructions
TIL_OPCODE_DEF(Goto)
TIL_OPCODE_DEF(Branch)
TIL_OPCODE_DEF(Return)

// pseudo-terms
TIL_OPCODE_DEF(Identifier)
TIL_OPCODE_DEF(IfThenElse)
TIL_OPCODE_DEF(Let)
#endif

// Binary operand emission.
size_t emitBinaryOpIntAdd(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initAdd(events, i, arg0, arg1);
}

size_t emitBinaryOpIntSub(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initSub(events, i, arg0, arg1);
}

size_t emitBinaryOpIntMul(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initMul(events, i, arg0, arg1);
}

size_t emitBinaryOpIntEq(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initEq(events, i, arg0, arg1);
}

size_t emitBinaryOpIntLt(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initLt(events, i, arg0, arg1);
}

size_t emitBinaryOpIntLeq(Event* events, size_t i, uint arg0, uint arg1) {
  return Event::initLe(events, i, arg0, arg1);
}

struct Block {
  static const size_t NO_DOMINATOR = (size_t)-1;
  BasicBlock* basicBlock;
  Block* list;
  size_t numArguments;
  size_t dominator;
  size_t head;
  size_t firstEvent;
  size_t lastEvent;
  size_t phiSlot;
};

size_t (*emitLiteralTable[32])(Event*, size_t, Literal&);
size_t (*emitUnaryOpIntTable[32])(Event*, size_t, uint);
size_t (*emitBinaryOpIntTable[32])(Event*, size_t, uint, uint);
size_t (*emitInstructionTable[32])(Event*, size_t, Instruction&);
size_t (*emitTerminatorTable[32])(Event*, size_t, Block&);

size_t emitBlockHeader(Event* events, size_t i, Block& block) {
  auto blocks = block.list;
  if (block.dominator == Block::NO_DOMINATOR)
    return Event::initNop(events, i, 0);
  if (blocks + block.head != &block)
    return Event::initWalkHeader(events, i, blocks[block.head].firstEvent);
  return Event::initGotoHeader(events, i, blocks[block.dominator].lastEvent);
}

size_t emitPhi(Event* events, size_t i, Phi&) {
  return Event::initPhi(events, i);
}

// Expression emission
size_t emitIntLiteral(Event* events, size_t i, Literal& literal) {
  return Event::initIntLiteral(events, i, (uint)literal.as<int>().value());
}

// TODO: handle vectors and sizes!
size_t emitLiteral(Event* events, size_t i, Instruction& instr) {
  auto literal = cast<Literal>(instr);
  return emitLiteralTable[literal.valueType().Base](events, i, literal);
}

size_t emitVariable(Event*, size_t i, Instruction&) {
  // TODO: emit reasonable error.
  return i;
}

size_t emitGoto(Event* events, size_t i, Block& block) {
  auto& targetBasicBlock =
      *cast<Goto>(block.basicBlock->terminator())->targetBlock();
  auto& targetBlock = block.list[targetBasicBlock.blockID()];
  auto phiSlot = block.phiSlot;
  auto phiOffset = targetBlock.firstEvent + 1;
  for (auto arg : targetBasicBlock.arguments())
    i = Event::initPhiCopy(
        events, i, cast<Instruction>(arg->values()[phiSlot].get())->stackID(),
        phiOffset++);
  return Event::initJump(events, i, targetBlock.firstEvent);
}

size_t emitBranch(Event* events, size_t i, Block& block) {
  auto& branch = *cast<Branch>(block.basicBlock->terminator());
  auto arg = cast<Instruction>(branch.condition())->stackID();
  auto& thenBlock = block.list[branch.thenBlock()->blockID()];
  auto& elseBlock = block.list[branch.elseBlock()->blockID()];
  return Event::initBranch(events, i, arg, thenBlock.firstEvent,
                           elseBlock.firstEvent);
}

size_t emitReturn(Event* events, size_t i, Block& block) {
  auto& ret = *cast<Return>(block.basicBlock->terminator());
  auto arg = cast<Instruction>(ret.returnValue())->stackID();
  return Event::initRet(events, i, arg);
}

size_t emitTerminator(Event* events, size_t i, Block& block) {
  return emitTerminatorTable[block.basicBlock->terminator()->opcode()](
    events, i, block);
}

size_t emitUnaryOp(Event* events, size_t i, Instruction& instr) {
  auto unaryOp = cast<UnaryOp>(instr);
  auto arg = cast<Instruction>(unaryOp.expr())->stackID();
  auto& type = unaryOp.valueType();
  return emitUnaryOpIntTable[unaryOp.unaryOpcode()](events, i, arg);
}

size_t emitBinaryOp(Event* events, size_t i, Instruction& instr) {
  auto binaryOp = cast<BinaryOp>(instr);
  auto arg0 = cast<Instruction>(binaryOp.expr0())->stackID();
  auto arg1 = cast<Instruction>(binaryOp.expr1())->stackID();
  auto& valueType = binaryOp.valueType();
  return emitBinaryOpIntTable[binaryOp.binaryOpcode()](events, i, arg0, arg1);
}

size_t emitInstruction(Event* events, size_t i, Instruction& instr) {
  return (emitInstructionTable[instr.opcode()])(events, i, instr);
}

void initTables() {
  // Always instantiate to avoid race conditions.
  emitBinaryOpIntTable[BOP_Add] = emitBinaryOpIntAdd;
  emitBinaryOpIntTable[BOP_Sub] = emitBinaryOpIntSub;
  emitBinaryOpIntTable[BOP_Mul] = emitBinaryOpIntMul;
  emitBinaryOpIntTable[BOP_Eq] = emitBinaryOpIntEq;
  emitBinaryOpIntTable[BOP_Lt] = emitBinaryOpIntLt;
  emitBinaryOpIntTable[BOP_Leq] = emitBinaryOpIntLeq;

  emitInstructionTable[COP_Literal] = emitLiteral;
  emitInstructionTable[COP_Variable] = emitVariable;
  emitInstructionTable[COP_UnaryOp] = emitUnaryOp;
  emitInstructionTable[COP_BinaryOp] = emitBinaryOp;

  emitTerminatorTable[COP_Goto] = emitGoto;
  emitTerminatorTable[COP_Branch] = emitBranch;
  emitTerminatorTable[COP_Return] = emitReturn;
}

void initBlock(Block* blocks, BasicBlock& basicBlock) {
  auto blockID = basicBlock.blockID();
  auto& block = blocks[basicBlock.blockID()];
  block.list = blocks;
  block.basicBlock = &basicBlock;
  block.numArguments = basicBlock.arguments().size();
  block.head = blockID;
  block.dominator = Block::NO_DOMINATOR;

  // Assign phi slots
  if (block.numArguments)
    for (size_t i = 0, e = basicBlock.predecessors().size(); i != e; ++i)
      blocks[basicBlock.predecessors()[i]->blockID()].phiSlot = i;

  // Assign events offsets.
  block.firstEvent = 0;
  size_t i = emitBlockHeader(nullptr, 0, block);
  for (auto arg : basicBlock.arguments()) i = emitPhi(nullptr, i, *arg);
  for (auto instr : basicBlock.instructions())
    i = emitInstruction(nullptr, i, *instr);
  i = emitTerminator(nullptr, i, block);
  block.lastEvent = i;
}

void initBlockDominators(Block& block) {
  auto& basicBlock = *block.basicBlock;
  if (BasicBlock* parent = basicBlock.parent()) {
    block.dominator = parent->blockID();
    if (basicBlock.postDominates(*parent) || block.dominator + 1 == block.head)
      block.head = block.list[block.dominator].head;
  }
}

size_t emitBlock(Event* events, size_t i, Block& block) {
  auto& basicBlock = *block.basicBlock;
  i = emitBlockHeader(events, block.firstEvent, block);
  for (auto arg : basicBlock.arguments())
    i = emitPhi(events, arg->setStackID(i), *arg);
  for (auto instr : basicBlock.instructions())
    i = emitInstruction(events, instr->setStackID(i), *instr);
  return emitInstruction(events, i, *basicBlock.terminator());
}

void emitEvents(Event* events, Global& global) {
  initTables();

  // Visit all of the CFGs
  VisitCFG visitCFG;
  visitCFG.traverseAll(global.global());
  auto& cfgs = visitCFG.cfgs();
  auto numCFGs = cfgs.size();
  if (!numCFGs) return;

  // Generate offsets for each CFG in the block array.
  auto cfgOffsets = new size_t[numCFGs + 1];
  *cfgOffsets = 0;
  auto temp = cfgOffsets + 1;
  for (auto& cfg : cfgs) {
    *temp = temp[-1] + cfg->numBlocks();
    temp++;  // TODO: collapse me
  }
  size_t numBlocks = cfgOffsets[numCFGs];

  // Allocate and initialize the 
  assert(numBlocks);
  Block* blocks = new Block[numBlocks];

  // Initialize the blocks (both loops are parallel safe)
  for (size_t i = 0; i < numCFGs; i++)
    for (auto& basicBlock : cfgs[i]->blocks())
      initBlock(blocks + cfgOffsets[i], *basicBlock.get());

  // Initialize the block dominators (outer loop is parallel safe)
  for (size_t i = 0; i < numCFGs; i++)
    for (auto& basicBlock : cfgs[i]->blocks())
      initBlockDominators(blocks[cfgOffsets[i] + basicBlock->blockID()]);

  // Perform block prefix sum of block events.
  size_t i = 0;
  for (auto& block : AdaptRange(blocks, numBlocks)) {
    block.firstEvent = i;
    block.lastEvent = i = block.lastEvent + i;
  }
  if (i > MAX_EVENTS) {
    printf("Too many instructions for the backend to handle.\n");
    return;
  }

  // Emit the events.
  i = 0;
  for (auto& block : AdaptRange(blocks, numBlocks))
    i = emitBlock(events, i, block);

  // Clean up.
  delete[] cfgOffsets;
  delete[] blocks;
}
