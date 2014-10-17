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


// Create new blocks on demand, as we encounter jumps to them.
BasicBlock* CopyReducer::reduceWeak(BasicBlock *E) {
  auto *B = BlockMap[E->blockID()];
  if (!B) {
    // Create new block, and add all of its phi nodes to InstructionMap.
    // This has to be done before we process a Goto.
    unsigned Nargs = E->arguments().size();
    B = newBlock(Nargs, E->numPredecessors());
    BlockMap[E->blockID()] = B;

    for (unsigned i = 0; i < Nargs; ++i) {
      Phi *Ph = E->arguments()[i];
      if (Ph && Ph->instrID() > 0)
        InstructionMap[Ph->instrID()] = B->arguments()[i];
    }
  }
  return B;
}

}  // end namespace ohmu
