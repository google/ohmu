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


struct BlockInfo {
  std::vector<SExpr*> AllocVarMap;
};

class SSAPass {
public:
  SExpr* reduceAlloc(Alloc &Orig, SExpr* E0) {
    Orig.setAllocPos(CurrentVarMap->size());
    return Orig;
  }

  SExpr* reduceLoad(Load &Orig, SExpr* E0) {
    if (Alloc* A = dyn_cast<Alloc>(E0)) { 
      if (auto *Av = (*CurrentVarMap)[A->allocPos()])
        return Av;   // Replace load with current value.
    }
    return Orig;
  }

private:
  std::vector<BlockInfo> BlockInfo
  std::vector<SExpr*>*   CurrentVarMap;
};


}  // end namespace ohmu

#endif  // OHMU_TIL_SSAPASS_H
