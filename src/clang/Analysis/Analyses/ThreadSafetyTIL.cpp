//===- ThreadSafetyTIL.cpp -------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"

namespace clang {
namespace threadSafety {
namespace til {


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
    case BOP_LogicAnd: return "&&";
    case BOP_LogicOr:  return "||";
  }
  return "";
}


unsigned BasicBlock::addPredecessor(BasicBlock *Pred) {
  unsigned Idx = Predecessors.size();
  Predecessors.reserveCheck(1, Arena);
  Predecessors.push_back(Pred);
  for (SExpr *E : Args) {
    if (Phi* Ph = dyn_cast<Phi>(E)) {
      Ph->values().reserveCheck(1, Arena);
      Ph->values().push_back(nullptr);
    }
  }
  return Idx;
}


void BasicBlock::reservePredecessors(unsigned NumPreds) {
  Predecessors.reserve(NumPreds, Arena);
  for (SExpr *E : Args) {
    if (Phi* Ph = dyn_cast<Phi>(E)) {
      Ph->values().reserve(NumPreds, Arena);
    }
  }
}


// If E is a variable, then trace back through any aliases or redundant
// Phi nodes to find the canonical definition.
const SExpr *getCanonicalVal(const SExpr *E) {
  while (true) {
    if (auto *V = dyn_cast<Variable>(E)) {
      if (V->kind() == Variable::VK_Let) {
        E = V->definition();
        continue;
      }
    }
    if (const Phi *Ph = dyn_cast<Phi>(E)) {
      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        continue;
      }
    }
    break;
  }
  return E;
}



// If E is a variable, then trace back through any aliases or redundant
// Phi nodes to find the canonical definition.
// The non-const version will simplify incomplete Phi nodes.
SExpr *simplifyToCanonicalVal(SExpr *E) {
  while (auto *V = dyn_cast<Variable>(E)) {
    SExpr *D;
    do {
      if (V->kind() != Variable::VK_Let)
        return V;
      D = V->definition();
      auto *V2 = dyn_cast<Variable>(D);
      if (V2)
        V = V2;
      else
        break;
    } while (true);

    if (ThreadSafetyTIL::isTrivial(D))
      return D;

    if (Phi *Ph = dyn_cast<Phi>(D)) {
      if (Ph->status() == Phi::PH_Incomplete)
        simplifyIncompleteArg(V, Ph);

      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        continue;
      }
    }
    return V;
  }
  return E;
}



// Trace the arguments of an incomplete Phi node to see if they have the same
// canonical definition.  If so, mark the Phi node as redundant.
// getCanonicalVal() will recursively call simplifyIncompletePhi().
void simplifyIncompleteArg(Variable *V, til::Phi *Ph) {
  assert(Ph && Ph->status() == Phi::PH_Incomplete);

  // eliminate infinite recursion -- assume that this node is not redundant.
  Ph->setStatus(Phi::PH_MultiVal);

  SExpr *E0 = simplifyToCanonicalVal(Ph->values()[0]);
  for (unsigned i=1, n=Ph->values().size(); i<n; ++i) {
    SExpr *Ei = simplifyToCanonicalVal(Ph->values()[i]);
    if (Ei == V)
      continue;  // Recursive reference to itself.  Don't count.
    if (Ei != E0) {
      return;    // Status is already set to MultiVal.
    }
  }
  Ph->setStatus(Phi::PH_SingleVal);
  // Eliminate Redundant Phi node.
  V->setDefinition(Ph->values()[0]);
}



int BasicBlock::renumberVars(int ID) {
  for (auto *E : Args)
    E->setID(this, ID++);
  for (auto *E : Instrs)
    E->setID(this, ID++);
  TermInstr->setID(this, ID++);
  return ID;
}


void SCFG::renumberVars() {
  int ID = 0;
  for (auto *B : Blocks)
    ID = B->renumberVars(ID);
  NumInstructions = ID;
}

//void BasicBlock::computeDominator2() {
//  DominatorNode.SizeOfSubTree = 1;
//  for (auto B : Predecessors)
//    if (DominatorNode.SizeOfSubTree)
//      B->computeDominator2()
//}
//
//void SCFG::computeDominators2() {
//  Exit->computeDominator2();
//}

// Performs a topological walk of the CFG,
// as a reverse post-order depth-first traversal.
int BasicBlock::topologicalWalk(SimpleArray<BasicBlock*>& Blocks, int ID) {
  if (Visited) return ID;
  Visited = 1;
  switch (terminator()->opcode()) {
    case COP_Goto:
      ID = cast<Goto>(terminator())->targetBlock()->topologicalWalk(Blocks, ID);
      break;
    case COP_Branch:
      ID = cast<Branch>(terminator())->elseBlock()->topologicalWalk(Blocks, ID);
      ID = cast<Branch>(terminator())->thenBlock()->topologicalWalk(Blocks, ID);
      break;
    case COP_Return:
      break;
    default:
      assert(false);
  }
  assert(ID > 0);
  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  Blocks[BlockID = --ID] = this;
  return ID;
}

// Performs a topological walk of the CFG,
// as a reverse post-order depth-first traversal.
// Assumes that there are no critical edges.
int BasicBlock::topologicalFinalSort(SimpleArray<BasicBlock*>& Blocks, int ID) {
  if (!Visited) return ID;
  Visited = 0;
  if (DominatorNode.Parent)
    ID = DominatorNode.Parent->topologicalFinalSort(Blocks, ID);
  for (auto *Pred : Predecessors)
    ID = Pred->topologicalFinalSort(Blocks, ID);
  assert(ID < Blocks.size());
  // set ID and update block array in place.
  // We may lose pointers to unreachable blocks.
  Blocks[BlockID = ID++] = this;
  return ID;
}


void SCFG::topologicalSort() {
  assert(valid());

  // TODO: this shouldn't be necessary
  for (BasicBlock *B : Blocks)
    B->Visited = 0;

  int BID = Entry->topologicalWalk(Blocks, Blocks.size());

  // If there were unreachable blocks, then ID will not be zero;
  // shift everything down, and delete unreachable blocks.
  if (BID > 0) {
    for (unsigned i = 0, n = Blocks.size()-BID; i < n; ++i) {
      Blocks[i] = Blocks[i+BID];
      Blocks[i]->BlockID = i;
    }
    Blocks.drop(BID);
  }
}



// Computes the immediate dominator of the current block.  Assumes that all of
// its predecessors have already computed their dominators.  This is achieved
// by topologically sorting the nodes and visiting them in order.
void BasicBlock::computeDominator() {
  BasicBlock *Candidate = nullptr;
  // Walk backwards from each predecessor to find the common dominator node.
  for (auto *Pred : Predecessors) {
    // Skip back-edges
    if (Pred->BlockID >= BlockID) continue;
    // If we don't yet have a candidate for dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Pred;
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Pred;
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

void BasicBlock::computePostDominator() {
#if 0
  BasicBlock *Candidate = nullptr;
  // Walk backwards from each predecessor to find the common dominator node.
  SimpleArray<BasicBlock*> Successors;
  switch (TermInstr->opcode()) {
  COP_Goto:
    PostDominatorNode.Parent = cast<Goto>(TermInstr)->targetBlock();
    PostDominatorNode.SizeOfSubTree = 1;
    return;
  COP_Branch:
    BasicBlock *Successors[] = {cast<Branch>(TermInstr)->thenBlock(),
                                cast<Branch>(TermInstr)->elseBlock()};
    for (auto Succ = &Successors[0], E = &Successors[2]; Succ != E; ++Succ) {
      // Skip back-edges
      if ((*Succ)->BlockID <= BlockID) continue;
      // If we don't yet have a candidate for dominator yet, take this one.
      if (Candidate == nullptr) {
        Candidate = (*Succ);
        continue;
      }
      // Walk the alternate and current candidate back to find a common ancestor.
      auto *Alternate = (*Succ);
      while (Alternate != Candidate) {
        if (Candidate->BlockID < Alternate->BlockID)
          Candidate = Candidate->PostDominatorNode.Parent;
        else
          Alternate = Alternate->PostDominatorNode.Parent;
      }
    }
    PostDominatorNode.Parent = Candidate;
    PostDominatorNode.SizeOfSubTree = 1;
    return;
    default:
      assert(false);
      return;
  }
#endif
}

// Computes dominators for all blocks in a CFG.  Assumes that the blocks have
// been topologically sorted.
void SCFG::computeDominators() {
  assert(valid());

  // Walk in topological order to calculate dominators.
  for (auto *B : Blocks)
    B->computeDominator();

  // Walk backwards through blocks, to calculate tree sizes.
  // Children will be visited before parents.
  for (size_t i = Blocks.size(); i > 0; --i) {
    assert(Blocks[i-1]);

    auto* Nd  = &Blocks[i-1]->DominatorNode;

    ++Nd->SizeOfSubTree;           // count the current block in its subtree

    if (Nd->Parent) {
      auto* PNd = &Nd->Parent->DominatorNode;
      // Initially set NodeID relative to the (as yet uncomputed) parent ID.
      Nd->NodeID = PNd->SizeOfSubTree;
      PNd->SizeOfSubTree += Nd->SizeOfSubTree;
    }
  }

  // Walk forwards again, and fixup NodeIDs relative to starting block.
  for (auto* B : Blocks) {
    if (B->DominatorNode.Parent)
      B->DominatorNode.NodeID += B->DominatorNode.Parent->DominatorNode.NodeID;
  }
}


#include <conio.h>
void SCFG::computeNormalForm() {
  int array[] = {0, 1, 2, 3, 4};
  SimpleArray<int> test(array, 5, 5);
  for (auto i : test)
    printf("%d ", i);
  printf("\n");
  for (auto i : SimpleArray<int>::ReverseAdaptor(test))
    printf("%d ", i);
  printf("\n");
  _getch();
#if 0
  Entry->topologicalWalk(Blocks, Blocks.size());
  for (auto *Block : Blocks) Block->computeDominator();
  Exit->topologicalFinalSort(Blocks, 0);
  for (auto **B = Blocks.end() - 1, **E = Blocks.begin(); B >= E; --B) {
    (*B)->computePostDominator();
    (*B)->DominatorNode.Parent->DominatorNode.SizeOfSubTree +=
        (*B)->DominatorNode.SizeOfSubTree;
  }
#endif
  //topologicalSort();
  //renumberVars();
  //computeDominators();
  Normal = true;
}



/*

static int topologicalPostWalk(BasicBlock *Parent, BasicBlock *Block, int BlockPostID) {
  if (Block->PostSortNode.SizeOfSubTree)
    return BlockPostID;
  int LastID = BlockPostID;
  for (auto Pred : Block->predecessors())
    // sort these based on DistanceToEntry...
    BlockPostID = topologicalPostWalk(Block, Pred, BlockPostID);
  Block->PostSortNode.NodeID = --BlockPostID;
  Block->PostSortNode.SizeOfSubTree = LastID - BlockPostID;
  Block->PostSortNode.Parent = Parent;
  return BlockPostID;
}


static void computePostDominator(BasicBlock *Block) {
  // Terminator is always defined because computePostDominator isn't called on the exit block.
  BasicBlock *Candidate = Block->PostSortNode.Parent;
  switch (Block->terminator()->opcode()) {
  case COP_Branch: {
    BasicBlock *ThenBlock = cast<Branch>(Block->terminator())->thenBlock();
    BasicBlock *ElseBlock = cast<Branch>(Block->terminator())->elseBlock();
    BasicBlock *Other = ThenBlock == Candidate ? ElseBlock : ThenBlock;
    while (Candidate != Other)
      if (Candidate->PostSortNode.NodeID > Other->PostSortNode.NodeID) {
        Candidate->PostDominatorNode.SizeOfSubTree -= Block->PostSortNode.SizeOfSubTree;
        Candidate = Candidate->PostDominatorNode.Parent;
      } else
        Other = Other->PostDominatorNode.Parent;
    break;
  }
  case COP_Goto:
    break;
  default:
    break;
  }
  Block->PostDominatorNode.SizeOfSubTree = Block->PostSortNode.SizeOfSubTree;
  Block->PostDominatorNode.Parent = Candidate;
}


static void computePostDominators(SCFG::BlockArray &Blocks) {
  Blocks[0]->PostDominatorNode = Blocks[0]->PostSortNode;
    computePostDominator(Blocks[1]);
  for (size_t i = 1, e = Blocks.size(); i != e; ++i)
    computePostDominator(Blocks[i]);
  for (size_t i = 1, e = Blocks.size(); i != e; ++i) {
    Blocks[i]->PostDominatorNode.NodeID = Blocks[i]->PostDominatorNode.Parent->PostDominatorNode.NodeID + 1;
    Blocks[i]->PostDominatorNode.Parent->PostDominatorNode.NodeID += Blocks[i]->PostDominatorNode.SizeOfSubTree;
  }
  for (auto Block : Blocks)
    Block->PostDominatorNode.NodeID -= Block->PostDominatorNode.SizeOfSubTree - 1;
}


static void sortBlocksByID(SCFG::BlockArray &Blocks, BasicBlock *BB) {
  if (Blocks[BB->blockID()])
    return;
  Blocks[BB->blockID()] = BB;
  for (auto Pred : BB->predecessors())
    sortBlocksByID(Blocks, Pred);
}

void SCFG::computeNormalForm() {
  topologicalWalk(nullptr, Entry, (int)Blocks.size());
  for (auto &Block : Blocks) {
    Block->setBlockID(Block->SortNode.NodeID);
    Block = nullptr;
  }
  sortBlocksByID(Blocks, Exit);
  computeDominators(Blocks);

  topologicalPostWalk(nullptr, Exit, (int)Blocks.size());
  for (auto &Block : Blocks) {
    Block->setBlockID(Block->PostSortNode.NodeID);
    Block = nullptr;
  }
  sortBlocksByID(Blocks, Exit);
  computePostDominators(Blocks);
  for (auto &Block : Blocks) {
    Block->setBlockID(Block->SortNode.NodeID);
    Block = nullptr;
  }
  sortBlocksByID(Blocks, Exit);

  //renumberVars();
  for (auto Block : Blocks) {
    printf("%2d %2d %2d   ", Block->SortNode.NodeID, Block->SortNode.SizeOfSubTree, Block->SortNode.Parent ? Block->SortNode.Parent->SortNode.NodeID : -1);
    printf("%2d %2d %2d   ", Block->DominatorNode.NodeID, Block->DominatorNode.SizeOfSubTree, Block->DominatorNode.Parent ? Block->DominatorNode.Parent->SortNode.NodeID : -1);
    printf("%2d %2d %2d   ", Block->PostSortNode.NodeID, Block->PostSortNode.SizeOfSubTree, Block->PostSortNode.Parent ? Block->PostSortNode.Parent->PostSortNode.NodeID : -1);
    printf("%2d %2d %2d\n" , Block->PostDominatorNode.NodeID, Block->PostDominatorNode.SizeOfSubTree, Block->PostDominatorNode.Parent ? Block->PostDominatorNode.Parent->PostSortNode.NodeID : -1);
  }
}

*/

}  // end namespace til
}  // end namespace threadSafety
}  // end namespace clang

