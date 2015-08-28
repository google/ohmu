//===- TIL.cpp -------------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//

#include "TIL.h"
#include "AnnotationImpl.h"
#include "CFGBuilder.h"

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
    case UOP_Negative: return "-";
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



bool SExpr::isTrivial() const {
  switch (Opcode) {
    case COP_ScalarType: return true;
    case COP_Literal:    return true;
    case COP_Variable:   return true;
    default:             return false;
  }
}


bool SExpr::isValue() const {
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


bool SExpr::isMemValue() const {
  switch (Opcode) {
    case COP_Function:   return true;
    case COP_Slot:       return true;
    case COP_Record:     return true;
    case COP_Code:       return true;
    case COP_Field:      return true;
    default:             return false;
  }
}

void SExpr::addAnnotation(Annotation *A) {
  if (A == nullptr)
    return;
  if (Annotations) {
    if (A->kind() < Annotations->kind()) {
      A->insert(Annotations);
      Annotations = A;
    } else {
      Annotations->insert(A);
    }
  }
  else
    Annotations = A;
}

SExpr* Future::addPosition(SExpr **Eptr) {
  // If the future has already been forced, return the forced value.
  if (Status == FS_done) {
    // The result may be a future, in which case we recurse.
    if (auto* Fut = dyn_cast_or_null<Future>(Result))
      return Fut->addPosition(Eptr);
    else
      return Result;
  }
  // Otherwise connect Eptr to this future, and return this future.
  Positions.push_back(Eptr);
  return this;
}


void Future::addInstrPosition(Instruction **Iptr) {
  assert(!IPos && "Future has already been added to a basic block.");

  // If this future has already been forced, return the result;
  if (Status == FS_done) {
    if (auto* Fut = dyn_cast_or_null<Future>(Result)) {
      Fut->addInstrPosition(Iptr);
      return;
    }
    else {
      auto *I = dyn_cast_or_null<Instruction>(Result);
      if (I && I->block() == nullptr && !Result->isTrivial()) {
        I->setBlock(this->block());
        *Iptr = I;
      }
      else {
        // If Result has already been added to a block, then it's a weak
        // reference to a previously added instruction; ignore it.
        *Iptr = nullptr;
      }
      return;
    }
  }
  IPos = Iptr;
}


void Future::setResult(SExpr *Res) {
  assert(Status != FS_done && "Future has already been forced.");

  auto *Fut = dyn_cast_or_null<Future>(Res);
  if (Fut) {
    // Result is another future; register all of our positions with it.
    if (IPos) {
      Fut->addInstrPosition(IPos);
    }
    for (SExpr **Eptr : Positions) {
      assert(*Eptr == this && "Invalid position for future.");
      *Eptr = Fut->addPosition(Eptr);
    }

    // This future may be a temporary object, so we don't call
    // Result = Fut->addPosition(&Result)
    Result = Fut;
  }
  else {
    // Write back result to basic block.
    if (IPos) {
      auto *I = dyn_cast_or_null<Instruction>(Res);
      if (I && I->block() == nullptr && !Res->isTrivial()) {
        assert(!isa<Phi>(I) && "Phi nodes are arguments.");
        I->setBlock(this->block());
        *IPos = I;
      }
      else {
        // If Result has already been added to a block, then it's a weak
        // reference to a previously added instruction; ignore it.
        *IPos = nullptr;
      }
      IPos = nullptr;
    }

    // Write back result to all positions that use this future.
    for (SExpr **Eptr : Positions) {
      assert(*Eptr == this && "Invalid position for future.");
      *Eptr = Res;
    }

    Result = Res;
  }

  Status = FS_done;

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
  return Result;
}



Slot* Record::findSlot(StringRef S) {
  // FIXME -- look this up in a hash table, please.
  for (auto &Slt : slots()) {
    if (Slt->slotName() == S)
      return Slt.get();
  }
  return nullptr;
}


std::pair<SExpr *, std::vector<SExpr *>> Call::arguments() {
  std::vector<SExpr *> Arguments;
  SExpr *E = Target.get();

  while (E && isa<Apply>(E)) {
    Arguments.emplace_back(cast<Apply>(E)->arg());
    E = cast<Apply>(E)->fun();
  }
  std::reverse(Arguments.begin(), Arguments.end());

  // Include self-argument if any.
  if (auto *Projection = dyn_cast<Project>(E)) {
    if (Projection->record()) {

      auto *Application = ohmu::cast<ohmu::til::Apply>(Projection->record());
      assert(Application->isSelfApplication());

      ohmu::til::SExpr *SelfArgument =
          Application->isDelegation() ? Application->arg() : Application->fun();
      Arguments.insert(Arguments.begin(), SelfArgument);
    }
  }

  return {E, Arguments};
}


/// Return the name (if any) of this instruction.
StringRef Instruction::instrName() const {
  const InstrNameAnnot* Name = getAnnotation<const InstrNameAnnot>();
  if (Name != nullptr)
    return Name->name();
  return StringRef("", 0);
}

/// Set the name for this instruction.
void Instruction::setInstrName(CFGBuilder &Builder, StringRef Name) {
  addAnnotation(Builder.newAnnotationT<InstrNameAnnot>(Name));
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
int BasicBlock::topologicalSort(BasicBlock** Blocks, int ID) {
  if (BlockID != InvalidBlockID) return ID;
  BlockID = 0;  // mark as being visited

  // First sort the post-dominator, if it exists.
  // This gives us a topological order where post-dominators always come last.
  if (PostDominatorNode.Parent)
    ID = PostDominatorNode.Parent->topologicalSort(Blocks, ID);

  for (auto &B : successors()) {
    if (B.get())
      ID = B->topologicalSort(Blocks, ID);
  }

  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  assert(ID > 0);
  BlockID = --ID;
  Blocks[BlockID] = this;
  return ID;
}


// Sorts blocks in post-topological order, by following predecessors.
// Each block will be written into the Blocks array in order, and PostBlockID
// will be set to the index in the array.  Sorting should start from the exit
// block, and ID should be the total number of blocks.
int BasicBlock::postTopologicalSort(BasicBlock** Blocks, int ID) {
  if (PostBlockID != InvalidBlockID) return ID;
  PostBlockID = 0;  // mark as being visited

  // First sort the dominator, if it exists.
  // This gives us a topological order where post-dominators always come last.
  if (DominatorNode.Parent)
    ID = DominatorNode.Parent->postTopologicalSort(Blocks, ID);

  for (auto &B : predecessors()) {
    if (B.get())
      ID = B->postTopologicalSort(Blocks, ID);
  }

  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  assert(ID > 0);
  PostBlockID = --ID;
  Blocks[PostBlockID] = this;
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
      if (!Alternate || !Candidate) {
        // TODO: warn on invalid CFG.
        Candidate = nullptr;
        break;
      }

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
    // Skip edges that have been pruned.
    if (Succ.get() == nullptr)
      continue;

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
      if (!Alternate || !Candidate) {
        // TODO: warn on invalid CFG.
        Candidate = nullptr;
        break;
      }

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
  // Clear existing block IDs.
  for (auto &B : Blocks) {
    B->BlockID     = BasicBlock::InvalidBlockID;
    B->PostBlockID = BasicBlock::InvalidBlockID;
  }

  // Allocate new vector to store the blocks in sorted order
  std::vector<BasicBlock*> Blks(Blocks.size(), nullptr);

  // Sort the blocks in post-topological order, starting from the exit.
  unsigned PostUnreachable = Exit->postTopologicalSort(&Blks[0], Blocks.size());

  // Fix up numbers if we have unreachable blocks.
  if (PostUnreachable > 0) {
    for (unsigned i = PostUnreachable, n = Blocks.size(); i < n; ++i)
      Blks[i]->PostBlockID -= PostUnreachable;
  }

  // Compute post-dominators, which improves the topological sort.
  for (unsigned i = PostUnreachable, n = Blocks.size(); i < n; ++i)
    Blks[i]->computePostDominator();

  // Now re-sort the blocks in topological order, starting from the entry.
  unsigned NumUnreachable = Entry->topologicalSort(&Blks[0], Blocks.size());

  // Collect any unreachable blocks, and fix up numbers.
  std::vector<BasicBlock*> Unreachables;
  if (NumUnreachable > 0) {
    for (unsigned i = NumUnreachable, n = Blocks.size(); i < n; ++i)
      Blks[i]->BlockID -= NumUnreachable;

    for (auto &B : Blocks) {
      if (B->BlockID == BasicBlock::InvalidBlockID)
        Unreachables.push_back(B.get());
    }
    assert(Unreachables.size() == NumUnreachable && "Error counting blocks.");
  }

  // Copy sorted blocks back to blocks array.
  int Bid = 0;
  int Nr  = Blocks.size() - NumUnreachable;
  for (; Bid < Nr;) {
    Blocks[Bid].reset( Blks[Bid + NumUnreachable] );
    ++Bid;
  }
  for (unsigned i = 0; i < NumUnreachable; ++i) {
    Blocks[Bid].reset( Unreachables[i] );
    ++Bid;
  }

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
