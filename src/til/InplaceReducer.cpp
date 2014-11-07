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

#include "InplaceReducer.h"

namespace ohmu {
namespace til  {


SCFG* InplaceReducer::reduceSCFG_Begin(SCFG &Orig) {
  beginCFG(&Orig);
  Scope->enterCFG(&Orig, &Orig);
  return &Orig;
}


SCFG* InplaceReducer::reduceSCFG_End(SCFG* Scfg) {
  Scope->exitCFG();
  endCFG();
  Scfg->renumber();
  return Scfg;
}


}  // end namespace til
}  // end namespace ohmu
