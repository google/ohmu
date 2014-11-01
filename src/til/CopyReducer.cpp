//===- CopyReducer.h -------------------------------------------*- C++ --*-===//
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
//
// CopyReducer implements the reducer interface to build a new SExpr;
// it makes a deep copy of a term.
//
// It is useful as a base class for more complex non-destructive rewrites.
//
//===----------------------------------------------------------------------===//

#include "til/CopyReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;


SCFG* CopyReducer::reduceSCFG_Begin(SCFG &Orig) {
  beginCFG(nullptr, Orig.numBlocks(), Orig.numInstructions());
  Scope->enterCFG(&Orig, currentCFG());
  return currentCFG();
}

SCFG* CopyReducer::reduceSCFG_End(SCFG* Scfg) {
  Scope->exitCFG();
  endCFG();
  Scfg->renumber();
  return Scfg;
}


BasicBlock* CopyReducer::reduceBasicBlockBegin(BasicBlock &Orig) {
  BasicBlock *B = reduceWeak(&Orig);
  beginBlock(B);
  return B;
}


BasicBlock* CopyReducer::reduceBasicBlockEnd(BasicBlock *B, SExpr* Term) {
  // Sanity check.
  // If Term isn't null, then writing the terminator should end the block.
  if (currentBB())
    endBlock(nullptr);
  return B;
}


// Create new blocks on demand, as we encounter jumps to them.
BasicBlock* CopyReducer::reduceWeak(BasicBlock *B) {
  auto *B2 = Scope->lookupBlock(B);
  if (!B2) {
    // Create new block, and add all of its Phi nodes to InstructionMap.
    // This has to be done before we process a Goto.
    unsigned Nargs = B->arguments().size();
    B2 = newBlock(Nargs, B->numPredecessors());
    Scope->updateBlockMap(B, B2);
  }
  return B2;
}


}  // end namespace ohmu
