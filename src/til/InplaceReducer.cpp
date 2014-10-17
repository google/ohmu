//===- InplaceReducer.cpp --------------------------------------*- C++ --*-===//
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

#include "til/InplaceReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;

/*
bool InplaceReducer::handleBBArg(BasicBlock &Orig, unsigned i, BasicBlock *BB,
                                 SExpr* E) {
  // Get the ID of the original argument.
  unsigned ID = BB->arguments()[i]->instrID();
  assert(CurrentInstrID == 0 || ID > CurrentInstrID &&
         "Instructions not numbered properly.");
  CurrentInstrID = ID;

  // Weak references to the original Phi node will return E;
  InstructionMap[ID] = E;

  if (Phi* Ph = dyn_cast<Phi>(E)) {
    // If this is an in-place rewrite of the original, then we're done.
    if (Ph == BB->arguments()[i])
      return true;

    // If this is a new Phi node,  then overwrite the original.
    if (!Ph->block()) {
      Ph->setBlock(BB);
      BB->arguments()[i] = Ph;
      return true;
    }
  }
  // Otherwise eliminate the Phi node.
  BB->arguments()[i] = nullptr;
  return false;
}


// This is called instead of handleResult for basic block instructions.
bool InplaceReducer::handleBBInstr(BasicBlock &Orig, unsigned i,
                                   BasicBlock *BB, SExpr* E) {
  // Get the ID of the original instruction.
  unsigned ID = BB->instructions()[i]->instrID();
  assert(CurrentInstrID == 0 || ID > CurrentInstrID &&
         "Instructions not numbered properly.");
  CurrentInstrID = ID;

  // Weak references to the original Instruction will return E;
  InstructionMap[ID] = E;

  if (Instruction *I = dyn_cast_or_null<Instruction>(E)) {
    // If this is an in-place rewrite of the original, then we're done.
    if (I == BB->instructions()[i])
      return true;

    // If this is a new, non-trivial instruction, then replace the original.
    if (I->block() == 0 && !I->isTrivial()) {
      assert(!isa<Phi>(I) && "Cannot insert Phi nodes as an instruction!");
      I->setBlock(BB);
      BB->instructions()[i] = I;
      return true;
    }
  }
  // Otherwise eliminate the instruction.
  BB->instructions()[i] = nullptr;
  return false;
}
*/


}  // end namespace ohmu
