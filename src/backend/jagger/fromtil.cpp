#include <cassert>
#include "types.h"
#include "til/til.h"
#include "til/Global.h"
#include "til/VisitCFG.h"
#include "util.h"

namespace Jagger {

uchar typeDesc(const ohmu::til::ValueType& type) {
  static const uchar jaggerType[ohmu::til::ValueType::BT_ValueRef + 1] = {
      BINARY_DATA,    BINARY_DATA,    UNSIGNED_INTEGER, FLOAT,
      SIGNED_INTEGER, SIGNED_INTEGER, SIGNED_INTEGER};
  static const uchar logBits[ohmu::til::ValueType::ST_128 + 1] = {
      LOG1 << 2,  LOG1 << 2,  LOG8 << 2,  LOG16 << 2,
      LOG32 << 2, LOG64 << 2, LOG128 << 2};
  uchar x = jaggerType[type.Base];
  if (type.Signed && type.Base == ohmu::til::ValueType::BT_Int)
    x = SIGNED_INTEGER;
  x |= logBits[type.Size];
  if (type.VectSize) {
    assert(!(type.VectSize & type.VectSize - 1));
    unsigned long size = type.VectSize;
    unsigned long log;
    _BitScanReverse(&log, size);
    x |= log << 5;
  }
  return x;
}

#if 0
size_t Event::initNop(Event* events, size_t i, uint payload) {
  if (!events) return i + 1;
  events[i + 0] = Event(NOP, payload);
  return i + 1;
}

size_t Event::initGotoHeader(Event* events, size_t i, uint target) {
  if (!events) return i + 1;
  events[i + 0] = Event(GOTO_HEADER, target);
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
  return i + 1;
}

size_t Event::initPhiCopy(Event* events, size_t i, uint arg0, uint phi) {
  if (!events) return i + 2;
  events[i + 0] = Event(JOIN_COPY | GPR, phi);
  events[i + 1] = Event(USE, arg0);
  return i + 2;
}

size_t Event::initJump(Event* events, size_t i, uint target) {
  if (!events) return i + 1;
  events[i + 0] = Event(JUMP, target);
  return i + 1;
}

size_t Event::initBranch(Event* events, size_t i, uint arg0, 
                         uint thenTarget, uint elseTarget) {
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
  events[i + 0] = Event(EQ, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}

size_t Event::initLt(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 4;
  events[i + 0] = Event(LT, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}

size_t Event::initLe(Event* events, size_t i, uint arg0, uint arg1) {
  if (!events) return i + 4;
  events[i + 0] = Event(LE, 0);
  events[i + 1] = Event(USE, arg0);
  events[i + 2] = Event(USE, arg1);
  events[i + 3] = Event(VALUE | FLAGS, i + 3);
  return i + 4;
}
#endif

#if 0
// Binary operand emission.
size_t emitBinaryOpIntAdd(EventBuilder builder, size_t i,
                          ohmu::til::BinaryOp& binaryOp) {
  binaryOp.valueType();
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return builder.binop(i, ADD, arg0->stackID(), arg1->stackID(), 0);
}

size_t emitBinaryOpIntSub(EventBuilder builder, size_t i,
                          ohmu::til::BinaryOp& binaryOp) {
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return EventBuilder::initSub(builder, i, arg0->stackID(), arg1->stackID());
}

size_t emitBinaryOpIntMul(EventBuilder builder, size_t i,
                          ohmu::til::BinaryOp& binaryOp) {
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return EventBuilder::initMul(builder, i, arg0->stackID(), arg1->stackID());
}

size_t emitBinaryOpIntEq(EventBuilder builder, size_t i,
                         ohmu::til::BinaryOp& binaryOp) {
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return EventBuilder::initEq(builder, i, arg0->stackID(), arg1->stackID());
}

size_t emitBinaryOpIntLt(EventBuilder builder, size_t i,
                         ohmu::til::BinaryOp& binaryOp) {
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return EventBuilder::initLt(builder, i, arg0->stackID(), arg1->stackID());
}

size_t emitBinaryOpIntLeq(EventBuilder builder, size_t i,
                          ohmu::til::BinaryOp& binaryOp) {
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  return EventBuilder::initLe(events, i, arg0->stackID(), arg1->stackID());
}
#endif

size_t (*emitLiteralTable[32])(EventBuilder, size_t, ohmu::til::Literal&);
size_t (*emitUnaryOpIntTable[32])(EventBuilder, size_t, ohmu::til::UnaryOp&);
size_t (*emitBinaryOpIntTable[32])(EventBuilder, size_t, ohmu::til::BinaryOp&);
size_t (*emitInstructionTable[32])(EventBuilder, size_t, ohmu::til::Instruction&);
size_t (*emitTerminatorTable[32])(EventBuilder, size_t, Block&);

size_t emitBlockHeader(EventBuilder builder, size_t i, Block& block) {
  auto blocks = block.list;
  if (block.dominator == Block::NO_DOMINATOR)
    return builder.op(i, NOP, 0);
  if (blocks + block.head != &block)
    return builder.op(i, WALK_HEADER, blocks[block.head].firstEvent);
  return builder.op(i, GOTO_HEADER, blocks[block.dominator].boundEvent);
}

size_t emitPhi(EventBuilder builder, size_t i, ohmu::til::Phi& phi) {
  return builder.op(i, PHI, typeDesc(phi.valueType()));
}

// Expression emission
size_t emitIntLiteral(EventBuilder builder, size_t i,
                      ohmu::til::Literal& literal) {
  return builder.op(i, BYTES4, (uint)literal.as<int>().value());
}

size_t emitInstruction(EventBuilder builder, size_t i,
                       ohmu::til::Instruction& instr) {
  return (emitInstructionTable[instr.opcode()])(builder, i, instr);
}

// TODO: handle vectors and sizes!
size_t emitLiteral(EventBuilder builder, size_t i,
                   ohmu::til::Instruction& instr) {
  auto& literal = *ohmu::cast<ohmu::til::Literal>(&instr);
  return emitLiteralTable[literal.valueType().Base](
      builder, literal.setStackID(i), literal);
}

size_t emitUnaryOp(EventBuilder builder, size_t i,
                   ohmu::til::Instruction& instr) {
  auto& unaryOp = *ohmu::cast<ohmu::til::UnaryOp>(&instr);
  auto arg = ohmu::cast<ohmu::til::Instruction>(unaryOp.expr());
  if (arg->isTrivial()) i = emitInstruction(builder, i, *arg);
  return emitUnaryOpIntTable[unaryOp.unaryOpcode()](
      builder, unaryOp.setStackID(i), unaryOp);
}

size_t emitBinaryOp(EventBuilder builder, size_t i,
                    ohmu::til::Instruction& instr) {
  auto& binaryOp = *ohmu::cast<ohmu::til::BinaryOp>(&instr);
  auto arg0 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr0());
  auto arg1 = ohmu::cast<ohmu::til::Instruction>(binaryOp.expr1());
  if (arg0->isTrivial()) i = emitInstruction(builder, i, *arg0);
  if (arg1->isTrivial()) i = emitInstruction(builder, i, *arg1);
  static const Opcode opcodeTable[] = {
    /* BOP_Add      =*/ ADD,
    /* BOP_Sub      =*/ SUB,
    /* BOP_Mul      =*/ MUL,
    /* BOP_Div      =*/ DIV,
    /* BOP_Rem      =*/ IMOD,
    /* BOP_Shl      =*/ NOP,
    /* BOP_Shr      =*/ NOP,
    /* BOP_BitAnd   =*/ LOGIC,
    /* BOP_BitXor   =*/ LOGIC,
    /* BOP_BitOr    =*/ LOGIC,
    /* BOP_Eq       =*/ COMPARE,
    /* BOP_Neq      =*/ COMPARE,
    /* BOP_Lt       =*/ COMPARE,
    /* BOP_Leq      =*/ COMPARE,
    /* BOP_Gt       =*/ COMPARE,
    /* BOP_Geq      =*/ COMPARE,
    /* BOP_LogicAnd =*/ LOGIC,
    /* BOP_LogicOr  =*/ LOGIC,
  };
  static const uchar controlTable[] = {
    /* BOP_Add      =*/ 0,
    /* BOP_Sub      =*/ 0,
    /* BOP_Mul      =*/ 0,
    /* BOP_Div      =*/ 0,
    /* BOP_Rem      =*/ 0,
    /* BOP_Shl      =*/ 0,
    /* BOP_Shr      =*/ 0,
    /* BOP_BitAnd   =*/ LOGICAL_AND,
    /* BOP_BitXor   =*/ LOGICAL_XOR,
    /* BOP_BitOr    =*/ LOGICAL_OR,
    /* BOP_Eq       =*/ CMP_EQ,
    /* BOP_Neq      =*/ CMP_NEQ,
    /* BOP_Lt       =*/ CMP_LT,
    /* BOP_Leq      =*/ CMP_LE,
    /* BOP_Gt       =*/ CMP_GT,
    /* BOP_Geq      =*/ CMP_GE,
    /* BOP_LogicAnd =*/ LOGICAL_AND,
    /* BOP_LogicOr  =*/ LOGICAL_OR,
  };
  Opcode code = opcodeTable[binaryOp.binaryOpcode()];
  i = builder.op(i, USE, arg0->stackID());
  i = builder.op(i, USE, arg1->stackID());
  if (code == COMPARE)
    return builder.op(i, code,
               CompareData(typeDesc(arg0->valueType()),
                           (Compare)controlTable[binaryOp.binaryOpcode()]));
  if (code == LOGIC)
    return builder.op(i, code,
                      LogicData(typeDesc(binaryOp.valueType()),
                                (Logic)controlTable[binaryOp.binaryOpcode()]));
  return builder.op(i, code, BasicData(typeDesc(binaryOp.valueType())));
}

size_t emitTerminator(EventBuilder builder, size_t i, Block& block) {
  return emitTerminatorTable[block.basicBlock->terminator()->opcode()](
      builder, i, block);
}

size_t emitGoto(EventBuilder builder, size_t i, Block& block) {
  auto& targetBasicBlock = *ohmu::cast<ohmu::til::Goto>(
                                block.basicBlock->terminator())->targetBlock();
  auto& targetBlock = block.list[targetBasicBlock.blockID()];
  auto phiSlot = block.phiSlot;
  auto phiOffset = targetBlock.firstEvent + 1;
  for (auto arg : targetBasicBlock.arguments()) {
    auto target = builder.root
                      ? ohmu::cast<ohmu::til::Instruction>(
                            arg->values()[phiSlot].get())->stackID()
                      : 0;
    i = builder.op(i, USE, target);
    i = builder.op(i, JOIN_COPY, phiOffset++);
  }
  return builder.op(i, JUMP, targetBlock.firstEvent);
}

size_t emitBranch(EventBuilder builder, size_t i, Block& block) {
  auto& branch = *ohmu::cast<ohmu::til::Branch>(block.basicBlock->terminator());
  auto arg = ohmu::cast<ohmu::til::Instruction>(branch.condition());
  if (arg->isTrivial()) i = emitInstruction(builder, i, *arg);
  auto& thenBlock = block.list[branch.thenBlock()->blockID()];
  auto& elseBlock = block.list[branch.elseBlock()->blockID()];
  i = builder.op(i, USE, arg->stackID());
  i = builder.op(i, BRANCH, elseBlock.firstEvent);
  i = builder.op(i, BRANCH_TARGET, thenBlock.firstEvent);
  return i;
}

size_t emitReturn(EventBuilder builder, size_t i, Block& block) {
  auto& ret = *ohmu::cast<ohmu::til::Return>(block.basicBlock->terminator());
  auto arg = ohmu::cast<ohmu::til::Instruction>(ret.returnValue());
  if (arg->isTrivial()) i = emitInstruction(builder, i, *arg);
  return builder.op(i, RET, arg->stackID());
}

void initTables() {
  // Always instantiate to avoid race conditions.
  //emitBinaryOpIntTable[ohmu::til::BOP_Add] = emitBinaryOpIntAdd;
  //emitBinaryOpIntTable[ohmu::til::BOP_Sub] = emitBinaryOpIntSub;
  //emitBinaryOpIntTable[ohmu::til::BOP_Mul] = emitBinaryOpIntMul;
  //emitBinaryOpIntTable[ohmu::til::BOP_Eq] = emitBinaryOpIntEq;
  //emitBinaryOpIntTable[ohmu::til::BOP_Lt] = emitBinaryOpIntLt;
  //emitBinaryOpIntTable[ohmu::til::BOP_Leq] = emitBinaryOpIntLeq;

  emitInstructionTable[ohmu::til::COP_Literal] = emitLiteral;
  emitInstructionTable[ohmu::til::COP_UnaryOp] = emitUnaryOp;
  emitInstructionTable[ohmu::til::COP_BinaryOp] = emitBinaryOp;

  emitLiteralTable[ohmu::til::ValueType::BT_Int] = emitIntLiteral;

  emitTerminatorTable[ohmu::til::COP_Goto] = emitGoto;
  emitTerminatorTable[ohmu::til::COP_Branch] = emitBranch;
  emitTerminatorTable[ohmu::til::COP_Return] = emitReturn;
}

size_t initBlock(Block* blocks, ohmu::til::BasicBlock& basicBlock) {
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
  EventBuilder builder(nullptr);
  size_t i = emitBlockHeader(builder, 0, block);
  for (auto arg : basicBlock.arguments()) i = emitPhi(builder, i, *arg);
  for (auto instr : basicBlock.instructions())
    i = emitInstruction(builder, i, *instr);
  i = emitTerminator(builder, i, block);
  return block.boundEvent = i;
}

void initBlockDominators(Block& block) {
  auto& basicBlock = *block.basicBlock;
  if (ohmu::til::BasicBlock* parent = basicBlock.parent()) {
    block.dominator = parent->blockID();
    if (basicBlock.postDominates(*parent) || block.dominator + 1 == block.head)
      block.head = block.list[block.dominator].head;
  }
}

size_t emitBlock(EventBuilder builder, size_t i, Block& block) {
  auto& basicBlock = *block.basicBlock;
  i = emitBlockHeader(builder, block.firstEvent, block);
  for (auto arg : basicBlock.arguments())
    i = emitPhi(builder, arg->setStackID(i), *arg);
  for (auto instr : basicBlock.instructions())
    i = emitInstruction(builder, i, *instr);
  return emitTerminator(builder, i, block);
}

extern void printDebug(EventBuilder builder, size_t numEvents);

void emitEvents(ohmu::til::Global& global) {
  initTables();

  // Visit all of the CFGs
  ohmu::til::VisitCFG visitCFG;
  visitCFG.traverseAll(global.global());
  auto& cfgs = visitCFG.cfgs();
  auto numCFGs = cfgs.size();
  if (!numCFGs) return;

  // Generate offsets for each CFG in the block array.
  auto cfgOffsets = new size_t[numCFGs + 1];
  *cfgOffsets = 0;
  for (size_t i = 0; i < numCFGs; i++)
    cfgOffsets[i + 1] = cfgOffsets[i] + cfgs[i]->numBlocks();
  auto numBlocks = cfgOffsets[numCFGs];

  // Allocate and initialize the 
  auto blocks = new Block[numBlocks];

  // Initialize the blocks (both loops are parallel safe)
  size_t numEvents = 0;
  for (size_t i = 0; i < numCFGs; i++)
    for (auto& basicBlock : cfgs[i]->blocks())
      numEvents += initBlock(blocks + cfgOffsets[i], *basicBlock.get());

  // Initialize the block dominators (outer loop is parallel safe)
  for (size_t i = 0; i < numCFGs; i++)
    for (auto& basicBlock : cfgs[i]->blocks())
      initBlockDominators(blocks[cfgOffsets[i] + basicBlock->blockID()]);

  // Perform block prefix sum of block events.
  size_t offset = (numEvents + 2) / 3;
  size_t i = offset;
  for (auto& block : AdaptRange(blocks, numBlocks)) {
    block.firstEvent = i;
    i = block.boundEvent += i;
    printf("block %d : %d-%d\n", block.basicBlock->blockID(), block.firstEvent,
           block.boundEvent);
  }

  auto eventBuffer = (char*)new int[(offset * 3 + 3) / 4 + numEvents];
  EventBuilder builder(eventBuffer - offset / 4 * 4);

  // Emit the events.
  i = offset;
  for (auto& block : AdaptRange(blocks, numBlocks))
    i = emitBlock(builder, i, block);

  printDebug(builder, numEvents);
  //delete[] eventBuffer;

  printf("numCFGs = %d\n", numCFGs);
  printf("numBlocks = %d\n", numBlocks);
  return;

  // Clean up.
  delete[] cfgOffsets;
  delete[] blocks;
}

} // namespace Jagger
