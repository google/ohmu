//===- TIL.cpp -------------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//

#include "TIL.h"

namespace ohmu {
namespace til  {


const char* BaseType::getTypeName() {
  switch (Base) {
    case BT_Void: return "Void";
    case BT_Bool: return "Bool";
    case BT_Int: {
      switch (Size) {
      case ST_8:  return "Int8";
      case ST_16: return "Int16";
      case ST_32: return "Int32";
      case ST_64: return "Int64";
      default:
        break;
      }
      break;
    }
    case BT_UnsignedInt: {
      switch (Size) {
      case ST_8:  return "UInt8";
      case ST_16: return "UInt16";
      case ST_32: return "UInt32";
      case ST_64: return "UInt64";
      default:
        break;
      }
      break;
    }
    case BT_Float: {
      switch (Size) {
      case ST_32: return "Float";
      case ST_64: return "Double";
      default:
        break;
      }
      break;
    }
    case BT_String:   return "String";
    case BT_Pointer:  return "Pointer";
  }
  return "InvalidType";
}


TIL_CastOpcode typeConvertable(BaseType Vt1, BaseType Vt2) {
  if (Vt1.isIntegral()) {
    if (Vt2.Base == Vt1.Base)
      if (Vt1.Size <= Vt2.Size)
        return CAST_extendNum;
    if (Vt2.Base == BaseType::BT_Float)
      if (static_cast<unsigned>(Vt1.Size) <= static_cast<unsigned>(Vt2.Size)-1)
        return CAST_extendToFloat;
  }
  else if (Vt1.Base == BaseType::BT_Float &&
           Vt2.Base == BaseType::BT_Float) {
    if (Vt1.Size <= Vt2.Size)
      return CAST_extendNum;
  }
  return CAST_none;
}



StringRef getOpcodeString(TIL_Opcode Op) {
  switch (Op) {
#define TIL_OPCODE_DEF(X)                                                   \
  case COP_##X:                                                             \
    return #X;
#include "TILOps.def"
#undef TIL_OPCODE_DEF
  }
  return "";
}


StringRef getUnaryOpcodeString(TIL_UnaryOpcode Op) {
  switch (Op) {
    case UOP_Minus:    return "-";
    case UOP_BitNot:   return "~";
    case UOP_LogicNot: return "!";
  }
  return "";
}


StringRef getBinaryOpcodeString(TIL_BinaryOpcode Op) {
  switch (Op) {
    case BOP_Mul:      return "*";
    case BOP_Div:      return "/";
    case BOP_Rem:      return "%";
    case BOP_Add:      return "+";
    case BOP_Sub:      return "-";
    case BOP_Shl:      return "<<";
    case BOP_Shr:      return ">>";
    case BOP_BitAnd:   return "&";
    case BOP_BitXor:   return "^";
    case BOP_BitOr:    return "|";
    case BOP_Eq:       return "==";
    case BOP_Neq:      return "!=";
    case BOP_Lt:       return "<";
    case BOP_Leq:      return "<=";
    case BOP_Gt:       return ">";
    case BOP_Geq:      return ">=";
    case BOP_LogicAnd: return "&&";
    case BOP_LogicOr:  return "||";
  }
  return "";
}


StringRef getCastOpcodeString(TIL_CastOpcode Op) {
  switch (Op) {
    case CAST_none:            return "none";
    case CAST_extendNum:       return "extendNum";
    case CAST_truncNum:        return "truncNum";
    case CAST_extendToFloat:   return "extendToFloat";
    case CAST_truncToFloat:    return "truncToFloat";
    case CAST_truncToInt:      return "truncToInt";
    case CAST_roundToInt:      return "roundToInt";
    case CAST_toBits:          return "toBits";
    case CAST_bitsToFloat:     return "bitsToFloat";
    case CAST_unsafeBitsToPtr: return "unsafeBitsToPtr";
    case CAST_downCast:        return "downCast";
    case CAST_unsafeDownCast:  return "unsafeDownCast";
    case CAST_unsafePtrCast:   return "unsafePtrCast";
    case CAST_objToPtr:        return "objToPtr";
  }
  return "";
}



bool SExpr::isTrivial() {
  switch (Opcode) {
    case COP_ScalarType: return true;
    case COP_Literal:    return true;
    case COP_Variable:   return true;
    default:             return false;
  }
}


bool SExpr::isValue() {
  switch (Opcode) {
    case COP_ScalarType: return true;
    case COP_Literal:    return true;
    case COP_Function:   return true;
    case COP_Slot:       return true;
    case COP_Record:     return true;
    case COP_Code:       return true;
    case COP_Field:      return true;
    default:             return false;
  }
}


bool SExpr::isHeapValue() {
  switch (Opcode) {
    case COP_Function:   return true;
    case COP_Slot:       return true;
    case COP_Record:     return true;
    case COP_Code:       return true;
    case COP_Field:      return true;
    default:             return false;
  }
}



SExpr* Future::addPosition(SExpr **Eptr) {
  // If the future has already been forced, return the forced value.
  if (Status == FS_done)
    return Result;
  // Otherwise connect Eptr to this future, and return this future.
  Positions.push_back(Eptr);
  return this;
}


void Future::setResult(SExpr *Res) {
  assert(Status != FS_done && "Future has already been forced.");

  Result = Res;
  Status = FS_done;

  if (IPos) {
    // If Res has already been added to a block, then it's a weak reference
    // to a previously added instruction; ignore it.
    if (auto *I = dyn_cast<Instruction>(Res)) {
      if (I->block() == nullptr && !Res->isTrivial()) {
        I->setBlock(block());
        *IPos = I;
      }
      else {
        *IPos = nullptr;
      }
    }
  }
  for (SExpr **Eptr : Positions) {
    assert(*Eptr == this && "Invalid position for future.");
    *Eptr = Res;
  }

  IPos = nullptr;
  Positions.clear();
  Positions.shrink_to_fit();
  assert(Positions.capacity() == 0 && "Memory Leak.");
}


SExpr* Future::force() {
  if (Status == Future::FS_done) {
    return Result;
  }
  if (Status == Future::FS_evaluating) {
    // TODO: print a useful diagnostic here.
    assert(false && "Infinite loop!");
    return nullptr;
  }

  Status = FS_evaluating;
  auto *Res = evaluate();
  setResult(Res);
  return Res;
}



Slot* Record::findSlot(StringRef S) {
  // FIXME -- look this up in a hash table, please.
  for (auto &Slt : slots()) {
    if (Slt->slotName() == S)
      return Slt.get();
  }
  return nullptr;
}



unsigned BasicBlock::findPredecessorIndex(const BasicBlock *BB) const {
  unsigned i = 0;
  for (auto &Pred : Predecessors) {
    if (Pred.get() == BB)
      return i;
    ++i;
  }
  return Predecessors.size();
}


unsigned BasicBlock::addPredecessor(BasicBlock *Pred) {
  unsigned Idx = Predecessors.size();
  Predecessors.emplace_back(Arena, Pred);
  for (Phi *Ph : Args) {
    assert(Ph->values().size() == Idx && "Phi nodes not sized properly.");
    Ph->values().emplace_back(Arena, nullptr);
  }
  return Idx;
}


void BasicBlock::reservePredecessors(unsigned NumPreds) {
  Predecessors.reserve(Arena, NumPreds);
  for (SExpr *E : Args) {
    if (Phi* Ph = dyn_cast<Phi>(E)) {
      Ph->values().reserve(Arena, NumPreds);
    }
  }
}



// Renumbers the arguments and instructions to have unique, sequential IDs.
unsigned BasicBlock::renumber(unsigned ID) {
  for (auto *Arg : Args) {
    if (!Arg)
      continue;
    Arg->setBlock(this);
    Arg->setInstrID(ID++);
  }
  for (auto *Instr : Instrs) {
    if (!Instr)
      continue;
    Instr->setBlock(this);
    Instr->setInstrID(ID++);
  }
  if (TermInstr)
    TermInstr->setInstrID(ID++);
  return ID;
}

// Renumber instructions in all blocks
void SCFG::renumber() {
  unsigned InstrID = 1;    // ID of 0 means unnumbered.
  unsigned BlockID = 0;
  for (auto &B : Blocks) {
    InstrID = B->renumber(InstrID);
    B->setBlockID(BlockID++);
  }
  NumInstructions = InstrID;
}


// Sorts blocks in topological order, by following successors.
// If post-dominators have been computed, it takes that into account.
// Each block will be written into the Blocks array in order, and its BlockID
// will be set to the index in the array.  Sorting should start from the entry
// block, and ID should be the total number of blocks.
int BasicBlock::topologicalSort(BlockArray& Blocks, int ID) {
  if (Visited) return ID;
  Visited = true;

  // First sort the post-dominator, if it exists.
  // This gives us a topological order where post-dominators always come last.
  if (PostDominatorNode.Parent)
    ID = PostDominatorNode.Parent->topologicalSort(Blocks, ID);

  for (auto &B : successors())
    ID = B->topologicalSort(Blocks, ID);

  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  assert(ID > 0);
  BlockID = --ID;
  Blocks[BlockID].reset(this);
  return ID;
}


// Sorts blocks in post-topological order, by following predecessors.
// Each block will be written into the Blocks array in order, and PostBlockID
// will be set to the index in the array.  Sorting should start from the exit
// block, and ID should be the total number of blocks.
int BasicBlock::postTopologicalSort(BlockArray& Blocks, int ID) {
  if (Visited) return ID;
  Visited = true;

  // First sort the dominator, if it exists.
  // This gives us a topological order where post-dominators always come last.
  if (DominatorNode.Parent)
    ID = DominatorNode.Parent->postTopologicalSort(Blocks, ID);

  for (auto &B : predecessors())
    ID = B->postTopologicalSort(Blocks, ID);

  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  assert(ID > 0);
  PostBlockID = --ID;
  Blocks[PostBlockID].reset(this);
  return ID;
}


// Computes the immediate dominator of the current block.  Assumes that all of
// its predecessors have already computed their dominators.  This is achieved
// by visiting the nodes in topological order.
void BasicBlock::computeDominator() {
  BasicBlock *Candidate = nullptr;
  // Walk backwards from each predecessor to find the common dominator node.
  for (auto &Pred : predecessors()) {
    // Skip back-edges
    if (Pred->BlockID >= BlockID) continue;
    // If we don't yet have a candidate for dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Pred.get();
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Pred.get();
    while (Alternate != Candidate) {
      if (Candidate->BlockID > Alternate->BlockID)
        Candidate = Candidate->DominatorNode.Parent;
      else
        Alternate = Alternate->DominatorNode.Parent;
    }
  }
  DominatorNode.Parent = Candidate;
  DominatorNode.SizeOfSubTree = 1;
}


// Computes the immediate post-dominator of the current block.  Assumes that all
// of its successors have already computed their post-dominators.  This is
// achieved visiting the nodes in reverse topological order.
void BasicBlock::computePostDominator() {
  BasicBlock *Candidate = nullptr;
  // Walk forward from each successor to find the common post-dominator node.
  for (auto &Succ : successors()) {
    // Skip back-edges
    if (Succ->PostBlockID >= PostBlockID) continue;
    // If we don't yet have a candidate for post-dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Succ.get();
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Succ.get();
    while (Alternate != Candidate) {
      if (Candidate->PostBlockID > Alternate->PostBlockID)
        Candidate = Candidate->PostDominatorNode.Parent;
      else
        Alternate = Alternate->PostDominatorNode.Parent;
    }
  }
  PostDominatorNode.Parent = Candidate;
  PostDominatorNode.SizeOfSubTree = 1;
}


static inline void computeNodeSize(BasicBlock *B,
                                   BasicBlock::TopologyNode BasicBlock::*TN) {
  BasicBlock::TopologyNode *N = &(B->*TN);
  if (N->Parent) {
    BasicBlock::TopologyNode *P = &(N->Parent->*TN);
    // Initially set ID relative to the (as yet uncomputed) parent ID
    N->NodeID = P->SizeOfSubTree;
    P->SizeOfSubTree += N->SizeOfSubTree;
  }
}

static inline void computeNodeID(BasicBlock *B,
                                 BasicBlock::TopologyNode BasicBlock::*TN) {
  BasicBlock::TopologyNode *N = &(B->*TN);
  if (N->Parent) {
    BasicBlock::TopologyNode *P = &(N->Parent->*TN);
    N->NodeID += P->NodeID;    // Fix NodeIDs relative to starting node.
  }
}


// Normalizes a CFG.  Normalization has a few major components:
// 1) Removing unreachable blocks.
// 2) Computing dominators and post-dominators
// 3) Topologically sorting the blocks into the "Blocks" array.
void SCFG::computeNormalForm() {
  // Sort the blocks in post-topological order, starting from the exit.
  int NumUnreachableBlocks = Exit->postTopologicalSort(Blocks, Blocks.size());
  assert(NumUnreachableBlocks == 0);

  // Compute post-dominators, which improves the topological sort.
  for (auto &B : Blocks) {
    B->computePostDominator();
    B->Visited = false;
  }

  // Now re-sort the blocks in topological order, starting from the entry.
  NumUnreachableBlocks = Entry->topologicalSort(Blocks, Blocks.size());
  assert(NumUnreachableBlocks == 0);

  // Renumber blocks and instructions now that we have a final sort.
  renumber();

  // Calculate dominators.
  // Compute sizes and IDs for the (post)dominator trees.
  for (auto &B : Blocks) {
    B->computeDominator();
    computeNodeSize(B.get(), &BasicBlock::PostDominatorNode);
  }
  for (auto &B : Blocks.reverse()) {
    computeNodeSize(B.get(), &BasicBlock::DominatorNode);
    computeNodeID(B.get(), &BasicBlock::PostDominatorNode);
  }
  for (auto &B : Blocks) {
    computeNodeID(B.get(), &BasicBlock::DominatorNode);
  }
}

}  // end namespace til
}  // end namespace ohmu
