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
  for (auto *Arg : Args)
    Arg->setID(this, ID++);
  for (auto *Instr : Instrs)
    Instr->setID(this, ID++);
  TermInstr->setID(this, ID++);
  return ID;
}

// Performs a topological walk of the CFG,
// as a reverse post-order depth-first traversal.
int BasicBlock::topologicalWalk(SimpleArray<BasicBlock*>& Blocks, int ID) {
  if (Visited) return ID;
  Visited = 1;
  for (auto *Block : successors())
    ID = Block->topologicalWalk(Blocks, ID);
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
  BasicBlock *Candidate = nullptr;
  // Walk backwards from each predecessor to find the common dominator node.
  for (auto *Succ : successors()) {
    // Skip back-edges
    if (Succ->BlockID <= BlockID) continue;
    // If we don't yet have a candidate for dominator yet, take this one.
    if (Candidate == nullptr) {
      Candidate = Succ;
      continue;
    }
    // Walk the alternate and current candidate back to find a common ancestor.
    auto *Alternate = Succ;
    while (Alternate != Candidate) {
      if (Candidate->BlockID < Alternate->BlockID)
        Candidate = Candidate->PostDominatorNode.Parent;
      else
        Alternate = Alternate->PostDominatorNode.Parent;
    }
  }
  PostDominatorNode.Parent = Candidate;
  PostDominatorNode.SizeOfSubTree = 1;
}

void SCFG::computeNormalForm() {
  int NumUnreachableBlocks = Entry->topologicalWalk(Blocks, Blocks.size());
  // If there were unreachable blocks, then ID will not be zero;
  // shift everything down, and delete unreachable blocks.
  if (NumUnreachableBlocks > 0) {
    for (size_t I = NumUnreachableBlocks, E = Blocks.size(); I < E; ++I)
      Blocks[I - NumUnreachableBlocks] = Blocks[I];
    Blocks.drop(NumUnreachableBlocks);
  }
  for (auto *Block : Blocks) Block->computeDominator();
  int NumBlocks = Exit->topologicalFinalSort(Blocks, 0);
  assert(NumBlocks == Blocks.size());
  // Renumber the instructions after the final sort.
  int InstrID = 0;
  for (auto *Block : Blocks) InstrID = Block->renumberVars(InstrID);
  for (auto *Block : Blocks.reverse()) {
    Block->computePostDominator();
    Block->DominatorNode.addSizeToDominator();
  }
  for (auto *Block : Blocks) {
    Block->DominatorNode.assignDominatorID();
    Block->PostDominatorNode.addSizeToPostDominator();
  }
  for (auto *Block : Blocks.reverse())
    Block->PostDominatorNode.assignPostDominatorID();
}

}  // end namespace til
}  // end namespace threadSafety
}  // end namespace clang

