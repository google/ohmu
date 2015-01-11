#include "types.h"
#include "til/til.h"
#include "til/Global.h"
#include "til/VisitCFG.h"

static_assert(ohmu::til::COP_VarDecl    ==  0, "til changed");
static_assert(ohmu::til::COP_Function   ==  1, "til changed");
static_assert(ohmu::til::COP_Code       ==  2, "til changed");
static_assert(ohmu::til::COP_Field      ==  3, "til changed");
static_assert(ohmu::til::COP_Slot       ==  4, "til changed");
static_assert(ohmu::til::COP_Record     ==  5, "til changed");
static_assert(ohmu::til::COP_ScalarType ==  6, "til changed");
static_assert(ohmu::til::COP_SCFG       ==  7, "til changed");
static_assert(ohmu::til::COP_BasicBlock ==  8, "til changed");
static_assert(ohmu::til::COP_Literal    ==  9, "til changed");
static_assert(ohmu::til::COP_Variable   == 10, "til changed");
static_assert(ohmu::til::COP_Apply      == 11, "til changed");
static_assert(ohmu::til::COP_Project    == 12, "til changed");
static_assert(ohmu::til::COP_Call       == 13, "til changed");
static_assert(ohmu::til::COP_Alloc      == 14, "til changed");
static_assert(ohmu::til::COP_Load       == 15, "til changed");
static_assert(ohmu::til::COP_Store      == 16, "til changed");
static_assert(ohmu::til::COP_ArrayIndex == 17, "til changed");
static_assert(ohmu::til::COP_ArrayAdd   == 18, "til changed");
static_assert(ohmu::til::COP_UnaryOp    == 19, "til changed");
static_assert(ohmu::til::COP_BinaryOp   == 20, "til changed");
static_assert(ohmu::til::COP_Cast       == 21, "til changed");
static_assert(ohmu::til::COP_Phi        == 22, "til changed");
static_assert(ohmu::til::COP_Goto       == 23, "til changed");
static_assert(ohmu::til::COP_Branch     == 24, "til changed");
static_assert(ohmu::til::COP_Return     == 25, "til changed");
static_assert(ohmu::til::COP_Future     == 26, "til changed");
static_assert(ohmu::til::COP_Undefined  == 27, "til changed");
static_assert(ohmu::til::COP_Wildcard   == 28, "til changed");
static_assert(ohmu::til::COP_Identifier == 29, "til changed");
static_assert(ohmu::til::COP_Let        == 30, "til changed");
static_assert(ohmu::til::COP_Letrec     == 31, "til changed");
static_assert(ohmu::til::COP_IfThenElse == 32, "til changed");

namespace jagger {
namespace {
struct ModuleBuilder {
  struct BlockSidecar {
    ohmu::til::BasicBlock* basicBlock;
    uint entryBlockID;
    uint firstPredecessor;
    uint firstSuccessor;
    uint boundSuccessor;
  };
  ModuleBuilder(wax::Module& module, ohmu::til::Global& global)
      : module(module), global(global) {
  }
  ModuleBuilder(const ModuleBuilder&) = delete;
  ModuleBuilder& operator =(const ModuleBuilder&) = delete;
  void walkTILGraph();
  void buildFunctionArray();
  void buildBlockSidecarArray();
  void buildBlockArray();
  void countEvents();
  void buildEventArray();
  wax::Module& module;
  ohmu::til::Global& global;
  ohmu::til::VisitCFG visitCFG;
  Array<BlockSidecar> blockSidecarArray;
};

void ModuleBuilder::walkTILGraph() {
  visitCFG.traverseAll(global.global());
  if (visitCFG.cfgs().empty())
    error("Can't build a module without any input.");
}

void ModuleBuilder::buildFunctionArray() {
  module.functionArray = Array<wax::Function>(visitCFG.cfgs().size());
  auto i = module.functionArray.begin();
  for (auto cfg : visitCFG.cfgs()) i++->blocks.bound = cfg->numBlocks();
  module.functionArray[0].blocks.first = 0;
  for (auto& fun : module.functionArray.slice(1, -1))
    fun.blocks.bound += fun.blocks.first = (&fun)[-1].blocks.bound;
}

void ModuleBuilder::buildBlockSidecarArray() {
  auto& functionArray = module.functionArray;
  blockSidecarArray = Array<BlockSidecar>(functionArray.last().blocks.bound);
  auto function = functionArray.begin();
  auto origin = blockSidecarArray.begin();
  for (auto cfg : visitCFG.cfgs()) {
    uint entryBlockID = function++->blocks.first;
    for (auto& sidecar : blockSidecarArray) {
      auto basicBlock = cfg->blocks()[&sidecar - (origin + entryBlockID)].get();
      sidecar.entryBlockID = entryBlockID;
      sidecar.basicBlock = basicBlock;
      sidecar.firstSuccessor = basicBlock->predecessors().size();
      sidecar.boundSuccessor = basicBlock->successors().size();
    }
  }
  blockSidecarArray[0].firstPredecessor = 0;
  blockSidecarArray[0].boundSuccessor += blockSidecarArray[0].firstSuccessor;
  for (auto& sidecar : blockSidecarArray.slice(1, -1))
    sidecar.boundSuccessor += sidecar.firstSuccessor +=
        sidecar.firstPredecessor = (&sidecar)[-1].boundSuccessor;
}

void ModuleBuilder::buildBlockArray() {
  module.blockArray = Array<wax::Block>(blockSidecarArray.size);
  module.neighborArray =
      Array<uint>(blockSidecarArray.last().boundSuccessor);
  auto sidecar = blockSidecarArray.begin();
  auto blocks = module.blockArray.begin();
  auto neighbors = module.neighborArray.begin();
  for (auto& block : module.blockArray) {
    block.predecessors.first = sidecar->firstPredecessor;
    block.predecessors.bound = sidecar->firstSuccessor;
    block.successors.first = sidecar->firstSuccessor;
    block.successors.bound = sidecar->boundSuccessor;
    block.blockID = INVALID_INDEX;

    auto entryBlockID = sidecar->entryBlockID;
    auto succs = block.successors.bound;
    for (auto& tilSucc : sidecar->basicBlock->successors())
      neighbors[--succs] = entryBlockID + tilSucc->blockID();
    auto preds = block.predecessors.bound;
    for (auto& tilPred : sidecar->basicBlock->predecessors())
      neighbors[--preds] = entryBlockID + tilPred->blockID();

    if (!block.predecessors.size())
      block.caseIndex = INVALID_INDEX;
    if (!block.successors.size())
      block.phiIndex = INVALID_INDEX;
    for (auto& i : block.successors(neighbors))
      blocks[i].caseIndex = (uint)(&i - (neighbors + succs));
    for (auto& i : block.predecessors(neighbors))
      blocks[i].phiIndex = (uint)(&i - (neighbors + preds));

    sidecar++;
  }
}

//size_t countLiteral(const ohmu::til::Instruction& instr) {
//}
//
//size_t countLoad(const ohmu::til::Instruction& instr) {
//}
//
//size_t countStore(const ohmu::til::Instruction& instr) {
//}
//
#if 0
size_t(*emitEventsTable[ohmu::til::COP_NumOpcodes])(
  const ohmu::til::Instruction&) = {
  /*COP_VarDecl    =*/ nullptr,
  /*COP_Function   =*/ nullptr,
  /*COP_Code       =*/ nullptr,
  /*COP_Field      =*/ nullptr,
  /*COP_Slot       =*/ nullptr,
  /*COP_Record     =*/ nullptr,
  /*COP_ScalarType =*/ nullptr,
  /*COP_SCFG       =*/ nullptr,
  /*COP_BasicBlock =*/ nullptr,
  /*COP_Literal    =*/,
  /*COP_Variable   =*/ nullptr,
  /*COP_Apply      =*/ nullptr,
  /*COP_Project    =*/ nullptr,
  /*COP_Call       =*/ nullptr,
  /*COP_Alloc      =*/ nullptr,
  /*COP_Load       =*/,
  /*COP_Store      =*/,
  /*COP_ArrayIndex =*/ nullptr,
  /*COP_ArrayAdd   =*/ nullptr,
  /*COP_UnaryOp    =*/,
  /*COP_BinaryOp   =*/,
  /*COP_Cast       =*/ nullptr,
  /*COP_Phi        =*/ nullptr,
  /*COP_Goto       =*/,
  /*COP_Branch     =*/,
  /*COP_Return     =*/ nullptr,
  /*COP_Future     =*/ nullptr,
  /*COP_Undefined  =*/ nullptr,
  /*COP_Wildcard   =*/ nullptr,
  /*COP_Identifier =*/ nullptr,
  /*COP_Let        =*/ nullptr,
  /*COP_Letrec     =*/ nullptr,
  /*COP_IfThenElse =*/ nullptr,
};
#endif

void countEvents2(wax::Block& block, const ohmu::til::BasicBlock& basicBlock) {
  static const size_t table[] = {
    /*COP_VarDecl    =*/ 0,
    /*COP_Function   =*/ 0,
    /*COP_Code       =*/ 0,
    /*COP_Field      =*/ 0,
    /*COP_Slot       =*/ 0,
    /*COP_Record     =*/ 0,
    /*COP_ScalarType =*/ 0,
    /*COP_SCFG       =*/ 0,
    /*COP_BasicBlock =*/ 0,
    /*COP_Literal    =*/ wax::Load::SLOT_COUNT,
    /*COP_Variable   =*/ 0,
    /*COP_Apply      =*/ 0,
    /*COP_Project    =*/ 0,
    /*COP_Call       =*/ 0,
    /*COP_Alloc      =*/ 0,
    /*COP_Load       =*/ wax::Load::SLOT_COUNT,
    /*COP_Store      =*/ wax::Store::SLOT_COUNT,
    /*COP_ArrayIndex =*/ 0,
    /*COP_ArrayAdd   =*/ 0,
    /*COP_UnaryOp    =*/ wax::local::Unary<uint>::SLOT_COUNT,
    /*COP_BinaryOp   =*/ wax::local::Binary<uint>::SLOT_COUNT,
    /*COP_Cast       =*/ 0,
    /*COP_Phi        =*/ 0,
    /*COP_Goto       =*/ wax::Jump::SLOT_COUNT,
    /*COP_Branch     =*/ wax::Branch::SLOT_COUNT,
    /*COP_Return     =*/ 0,
    /*COP_Future     =*/ 0,
    /*COP_Undefined  =*/ 0,
    /*COP_Wildcard   =*/ 0,
    /*COP_Identifier =*/ 0,
    /*COP_Let        =*/ 0,
    /*COP_Letrec     =*/ 0,
    /*COP_IfThenElse =*/ 0,
  };

  static_assert(wax::CaseHeader::SLOT_COUNT == wax::JoinHeader::SLOT_COUNT,
    "Simplifies logic here.");
  size_t count = 0;
  if (block.caseIndex != INVALID_INDEX) count += wax::CaseHeader::SLOT_COUNT;
  count += block.predecessors.size() * wax::Phi::SLOT_COUNT;
  for (auto instr : basicBlock.instructions()) count += table[instr->opcode()];
  count += table[basicBlock.terminator()->opcode()];
  if (block.phiIndex == INVALID_INDEX) count += wax::Return::SLOT_COUNT;
  block.boundEvent = count;
}

void ModuleBuilder::countEvents() {
  auto blocks = module.blockArray.begin();
  auto sidecar = blockSidecarArray.begin();
  for (size_t i = 0, e = module.blockArray.size; i != e; ++i)
    countEvents2(blocks[i], *sidecar[i].basicBlock);
  blocks[0].firstEvent = 0;
  for (size_t i = 1, e = module.blockArray.size; i != e; ++i)
    blocks[i].boundEvent += blocks[i].firstEvent = blocks[i - 1].boundEvent;
}
}  // namespace {
void buildModuleFromTIL(wax::Module& module, ohmu::til::Global& global) {
  ModuleBuilder builder(module, global);
  builder.walkTILGraph();
  builder.buildFunctionArray();
  builder.buildBlockSidecarArray();
  builder.buildBlockArray();
  //builder.buildEventArray();
  builder.countEvents();

}
}  // namespace jagger

#if 0
namespace Jagger {
//extern void printDebug(EventBuilder builder, size_t numEvents);
//extern void normalize(const EventList& in);

struct Block {
  static const size_t NO_DOMINATOR = (size_t)-1;
  ohmu::til::BasicBlock* basicBlock;
  Block* list;
  size_t dominator;
  size_t head;
  size_t firstEvent;
  size_t boundEvent;
  size_t phiSlot;
};

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
    return builder.op(i, JOIN_HEADER, blocks[block.head].boundEvent);
  if (blocks[block.dominator].boundEvent == block.firstEvent)
    return builder.op(i, NOP, 0);
  return builder.op(i, CASE_HEADER, blocks[block.dominator].boundEvent);
}

size_t emitPhi(EventBuilder builder, size_t i, ohmu::til::Phi& phi) {
  return builder.op(i, PHI, 0);
}

// Expression emission
size_t emitIntLiteral(EventBuilder builder, size_t i,
                      ohmu::til::Literal& literal) {
  i = builder.op(i, IMMEDIATE_BYTES, (uint)literal.as<int>().value());
  return builder.op(i, ANCHOR, 0);
}

size_t emitInstruction(EventBuilder builder, size_t i,
                       ohmu::til::Instruction& instr) {
  return (emitInstructionTable[instr.opcode()])(builder, i, instr);
}

// TODO: handle vectors and sizes!
size_t emitLiteral(EventBuilder builder, size_t i,
                   ohmu::til::Instruction& instr) {
  auto& literal = *ohmu::cast<ohmu::til::Literal>(&instr);
  i = emitLiteralTable[literal.valueType().Base](builder, i, literal);
  literal.setStackID(i - 1);
  return i;
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
  //TODO: pull these out and initialize them dynamically
  static const uchar opcodeTable[] = {
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
  uchar code = opcodeTable[binaryOp.binaryOpcode()];
  i = builder.op(i, LAST_USE, arg0->stackID());
  i = builder.op(i, LAST_USE, arg1->stackID());
  i = builder.op(
      i, code, code == COMPARE
                   ? (unsigned)CompareData(
                         typeDesc(arg0->valueType()),
                         (Compare)controlTable[binaryOp.binaryOpcode()])
                   : code == LOGIC
                         ? (unsigned)LogicData(
                               typeDesc(binaryOp.valueType()),
                               (Logic)controlTable[binaryOp.binaryOpcode()])
                         : (unsigned)BasicData(typeDesc(binaryOp.valueType())));
  binaryOp.setStackID(i);
  return builder.op(i, ANCHOR, 0);
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
    auto target =
        builder.empty() ? 0 : ohmu::cast<ohmu::til::Instruction>(
                                  arg->values()[phiSlot].get())->stackID();
    i = builder.op(i, LAST_USE, target);
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
  i = builder.op(i, LAST_USE, arg->stackID());
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
  block.head = blockID;
  block.dominator = Block::NO_DOMINATOR;

  // Assign phi slots
  if (basicBlock.arguments().size())
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
    //if (basicBlock.postDominates(*parent) || block.dominator + 1 == block.head)
    if (basicBlock.postDominates(*parent))
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

  // Check to see if we're going to run out of address space.
  if ((uint)numEvents != numEvents) {
    printf("Too many events!\n");
    delete[] cfgOffsets;
    delete[] blocks;
    return;
  }

  // Initialize the block dominators (outer loop is parallel safe)
  for (size_t i = 0; i < numCFGs; i++)
    for (auto& basicBlock : cfgs[i]->blocks())
      initBlockDominators(blocks[cfgOffsets[i] + basicBlock->blockID()]);

  // Perform block prefix sum of block events.
  EventList eventList;
  eventList.init(numEvents);
  size_t i = eventList.first;
  for (auto& block : AdaptRange(blocks, numBlocks)) {
    block.firstEvent = i;
    i = block.boundEvent += i;
    printf("block %d : %d-%d\n", block.basicBlock->blockID(), block.firstEvent,
           block.boundEvent);
  }

  // Emit the events.
  i = eventList.first;
  for (auto& block : AdaptRange(blocks, numBlocks))
    i = emitBlock(eventList.builder, i, block);

  normalize(eventList);
  printDebug(eventList.builder, numEvents);
  //delete[] eventBuffer;

  printf("numCFGs = %d\n", numCFGs);
  printf("numBlocks = %d\n", numBlocks);

  // Clean up.
  delete[] cfgOffsets;
  delete[] blocks;
}

} // namespace Jagger

#if 0
int buffer[1000];
for (size_t size = 1; size < 16; size++) {
  size_t o = (size + 2) / 3;
  auto p = (char*)buffer - o / 4 * 4;
  for (size_t i = 0; i < 1000; i++)
    buffer[i] = 0;
  for (size_t i = 0; i < size; i++) {
    p[i + o] = i + 1;
    ((unsigned*)p)[i + o] = (i + 1) * 0x1010101;
  }
  auto bound = ((o * 3 + 3) / 4 + size) * 4;
  //printf("%d : %d : %d\n", size, o, bound);
  if (bound > 80)
    bound = 80;
  for (size_t i = 0; i < bound; i++)
    printf("%x", 0xf & ((unsigned char*)buffer)[i]);
  if (bound != 80)
    printf("\n");
}
#endif
#endif