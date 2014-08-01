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
  while (auto *V = dyn_cast<Variable>(E)) {
    const SExpr *D;
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

    if (const Phi *Ph = dyn_cast<Phi>(D)) {
      if (Ph->status() == Phi::PH_SingleVal) {
        E = Ph->values()[0];
        continue;
      }
    }
    return V;
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
  if (Terminator.get())
    Terminator->setID(this, ID++);
  return ID;
}


void SCFG::renumberVars() {
  int ID = 0;
  for (auto *B : Blocks)
    ID = B->renumberVars(ID);
}



// Performs a topological walk of the CFG and produces a sort.  The sort is
// stored in SortNodes on the basic blocks.  These nodes store the topological
// index, the node's parent and the size of the topological subtree rooted at
// that node.  BlockID is the next avaliable id to grab during the post-order
// walk.  It's threaded as a return value.
static int topologicalWalk(BasicBlock *Parent, BasicBlock *Block, int ID) {
  // If the subtree size is not 0, we've already visited this node.
  if (Block->SortNode.SizeOfSubTree)
    return BlockID;
  // Via differencing, LastID is used to calculate the size of the subtree
  // rooted here.
  int LastID = BlockID;
  if (Block->terminator()) {
    // Walk our children, only if we've got a terminator.
    switch (Block->terminator()->opcode()) {
    case COP_Goto: {
      auto *Gt = cast<Goto>(Block->terminator())
      BlockID = topologicalWalk(Block, Gt->targetBlock(), BlockID);
      break;
    }
    case COP_Branch: {
      auto *Br = cast<Brach>(Block->terminator());
      // FIXME: sort these based on DistanceToExit...
      BlockID = topologicalWalk(Block, Br->elseBlock(), BlockID);
      BlockID = topologicalWalk(Block, Br->thenBlock(), BlockID);
      break;
    }
    default:
      break;
    }
  }
  // Initialize the SortNode with the information we calcluated here.
  Block->SortNode.NodeID = --BlockID;
  Block->SortNode.SizeOfSubTree = LastID - BlockID;
  Block->SortNode.Parent = Parent;
  return BlockID;
}



void SCFG::topologicalSort() {
  for (Block *BB :

  topologicalWalk(
}






/*


// Computes the immediate dominator of the current block.  Assumes that all of
// its predecessors have already computed their dominators.  This is achieved by
// topologically sorting the nodes and visiting them in order.
static void computeDominator(BasicBlock *Block) {
  // Predecessors is always non-empty because computeDominator isn't called on
  // the entry block.
  BasicBlock::BlockArray &Predecessors = Block->predecessors();
  BasicBlock *Candidate = Block->SortNode.Parent;
  for (auto Other : Predecessors)
    while (Candidate != Other)
      if (Candidate->SortNode.NodeID > Other->SortNode.NodeID) {
        Candidate->DominatorNode.SizeOfSubTree -= Block->SortNode.SizeOfSubTree;
        Candidate = Candidate->DominatorNode.Parent;
      } else
        Other = Other->DominatorNode.Parent;
  // Initialize everything except the NodeID in the DominatorNode.  The BlockID
  // is initalized on a separate pass.
  Block->DominatorNode.SizeOfSubTree = Block->SortNode.SizeOfSubTree;
  Block->DominatorNode.Parent = Candidate;
}


// Computes dominators for all blocks in a CFG, assumes that the blocks have
// been topologically sorted.
static void computeDominators(SCFG::BlockArray &Blocks) {
  // Copy the entry block sort node (Entry has no dominators).
  Blocks[0]->DominatorNode = Blocks[0]->SortNode;
  // Compute the dominators for all blocks, in topological order.
  for (size_t i = 1, e = Blocks.size(); i != e; ++i)
    computeDominator(Blocks[i]);
  // Allocate NodeIDs for all blocks.
  for (size_t i = 1, e = Blocks.size(); i != e; ++i) {
    // The NodeID allocate here is correct but will be altered by its children
    // and need to be fixed up.
    Blocks[i]->DominatorNode.NodeID =
        Blocks[i]->DominatorNode.Parent->DominatorNode.NodeID + 1;
    Blocks[i]->DominatorNode.Parent->DominatorNode.NodeID +=
        Blocks[i]->DominatorNode.SizeOfSubTree;
  }
  // Fixup the NodeIDs.
  for (auto Block : Blocks)
    Block->DominatorNode.NodeID -= Block->DominatorNode.SizeOfSubTree - 1;
}


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

