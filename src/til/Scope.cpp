//===- Scope.cpp -----------------------------------------------*- C++ --*-===//
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


#include "til/Scope.h"

namespace ohmu {

using namespace clang::threadSafety::til;


void ScopeFrame::enterCFG(SCFG *Orig, SCFG *S) {
  assert(!OrigCFG && "No support for nested CFGs");
  OrigCFG = Orig;
  InstructionMap.resize(Orig->numInstructions(), nullptr);
  BlockMap.resize(Orig->numBlocks(), nullptr);

  updateBlockMap(Orig->entry(), S->entry());
  updateBlockMap(Orig->exit(),  S->exit());
  updateInstructionMap(Orig->exit()->arguments()[0], S->exit()->arguments()[0]);
}


void ScopeFrame::exitCFG() {
  OrigCFG = nullptr;
  InstructionMap.clear();
  BlockMap.clear();
}


// Add B to BlockMap, and add its arguments to InstructionMap.
void ScopeFrame::updateBlockMap(BasicBlock *Orig, BasicBlock *B) {
  BlockMap[Orig->blockID()] = B;

  // Create a map from the arguments of Orig to the arguments of B
  unsigned Nargs = Orig->arguments().size();
  assert(Nargs == B->arguments().size() && "Block arguments don't match.");

  for (unsigned i = 0; i < Nargs; ++i) {
    Phi *Ph = Orig->arguments()[i];
    if (Ph && Ph->instrID() > 0)
      InstructionMap[Ph->instrID()] = B->arguments()[i];
  }
}



void ScopeHandler::enterScope(VarDecl *Orig, VarDecl *Nv) {
  // Skip unnamed, unnumbered let variables.
  if (Orig->varIndex() == 0 && Orig->kind() == VarDecl::VK_Let &&
      Orig->name().length() == 0)
    return;

  Scope->enterScope(Orig, Nv);

  // Copy names of let-variables to their definitions.
  if (Nv->kind() == VarDecl::VK_Let && Nv->definition()) {
    if (Instruction *I = dyn_cast<Instruction>(Nv->definition()))
      if (I->instrName().length() == 0)
        I->setInstrName(Nv->name());
  }
}


void ScopeHandler::exitScope(VarDecl *Orig) {
  Scope->exitScope(Orig);
}


}  // end namespace ohmu

