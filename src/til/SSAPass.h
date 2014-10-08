//===- SSAPass.h -----------------------------------------------*- C++ --*-===//
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
// Implements the conversion to SSA.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_SSAPASS_H
#define OHMU_TIL_SSAPASS_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"

#include "til/InplaceReducer.h"

namespace ohmu {

using namespace clang::threadSafety::til;


typedef std::vector<SExpr*> LocalVarMap;

struct BlockInfo {
  LocalVarMap AllocVarMap;
};


/// Base class for SSA Passes.
/// Conversion to SSA is done in two passes.  The first populates the
/// initial lookup tables for each block, while the second
class SSAPassBase : public InplaceReducer {
public:
  SSAPassBase()
    : CurrentCFG(nullptr), CurrentBB(nullptr), CurrentBlockID(0)
  { }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    assert(CurrentBB == nullptr && "Already in a basic block.");
    CurrentBB = &Orig;
    CurrentBlockID = CurrentBB->blockID();
    return &Orig;
  }

  BasicBlock* reduceBasicBlock(BasicBlock *BB) {
    assert(CurrentBB == BB && "Internal traversal error.");
    return BB;
  }

protected:
  SCFG*        CurrentCFG;
  BasicBlock*  CurrentBB;
  unsigned     CurrentBlockID;
};



class SSAPass : public SSAPassBase {
public:
  SSAPass() : CurrentVarMap(nullptr) { }

  SCFG* reduceSCFGBegin(SCFG &Orig) {
    assert(CurrentCFG == nullptr && "Already in a CFG.");
    CurrentCFG = &Orig;
    BInfoMap.resize(CurrentCFG->numBlocks());
    return &Orig;
  }

  SCFG* reduceSCFG(SCFG* Scfg) {
    assert(Scfg == CurrentCFG && "Internal traversal error.");
    CurrentCFG = nullptr;
    BInfoMap.clear();
    return Scfg;
  }

  BasicBlock* reduceBasicBlockBegin(BasicBlock &Orig) {
    SSAPassBase::reduceBasicBlockBegin(Orig);
    // warning -- make sure you don't add blocks to BlockInfo.
    CurrentVarMap = &BInfoMap[CurrentBlockID].AllocVarMap;

    // Initialize variable map to the size of the dominator's map.
    // Local variables in the dominator are in scope.
    unsigned PSize = 0;
    if (CurrentBB->parent())
      PSize = BInfoMap[CurrentBB->parent()->blockID()].AllocVarMap.size();
    CurrentVarMap->resize(PSize, nullptr);
    return &Orig;
  }

  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    if (CurrentBB) {
      // Add alloc to current var map.
      Orig.setAllocID(CurrentVarMap->size());
      CurrentVarMap->push_back(E0);
    }
    return &Orig;
  }

  SExpr* reduceStore(Store &Orig, SExpr* E0, SExpr* E1) {
    if (CurrentBB) {
      // Update current var map.
      if (auto* A = dyn_cast<Alloc>(E0)) {
        CurrentVarMap->at(A->allocID()) = E1;
      }
    }
    return &Orig;
  }

  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    if (CurrentBB) {
      // Replace load with value from current var map.
      if (auto* A = dyn_cast<Alloc>(E0)) {
        if (auto *Av = CurrentVarMap->at(A->allocID()))
          return Av;
      }
    }
    return &Orig;
  }

private:
  LocalVarMap* CurrentVarMap;
  std::vector<BlockInfo> BInfoMap;
};



class SSALookupPass : public SSAPassBase {
public:
  SSALookupPass(std::vector<BlockInfo>& BMap) : BInfoMap(BMap) { }

  // Lookup value of local variable at the beginning of basic block B
  SExpr* lookupBegin(BasicBlock *B, unsigned LvarID) {
    return nullptr;
  }

  // Lookup value of local variable at the end of basic block B
  SExpr* lookupEnd(BasicBlock *B, unsigned LvarID) {
    auto* LvarMap = &BInfoMap[B->blockID()].AllocVarMap;
    if (LvarID >= LvarMap->size())
      return nullptr;
    // Check to see if the variable was set in this block.
    if (auto* E = LvarMap->at(LvarID))
      return E;
    // Lookup variable in predecessor blocks, and store in the end map.
    auto* E = lookupBegin(B, LvarID);
    LvarMap->at(LvarID) = E;
    return E;
  }

private:
  std::vector<BlockInfo>& BInfoMap;
};


}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
