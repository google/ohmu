//===- VisitCFG.h --------------------------------------------*- C++ --*-===//
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
// This is a simple Visitor class that collects all of the CFGs in a module
// into a list.
//
//===----------------------------------------------------------------------===//

#include "TIL.h"
#include "TILTraverse.h"

#ifndef OHMU_TIL_VISITCFG_H_
#define OHMU_TIL_VISITCFG_H_

namespace ohmu {
namespace til  {


// Simple visitor which finds all CFGs and adds them to list.
class VisitCFG : public VisitReducer<VisitCFG> {
public:
  // Don't traverse inside CFGs; just add them to the list.
  bool traverseSCFG(SCFG* cfg, TraversalKind k) {
    cfgList_.push_back(cfg);
    return true;
  }

  std::vector<SCFG*>& cfgs() { return cfgList_; }

private:
  std::vector<SCFG*> cfgList_;
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_VISITCFG_H_
