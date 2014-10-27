//===- Global.h ------------------------------------------------*- C++ --*-===//
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

#ifndef OHMU_TIL_GLOBAL_H
#define OHMU_TIL_GLOBAL_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"

#include <ostream>

namespace ohmu {

using namespace clang::threadSafety::til;


class Global {
public:
  Global()
      : GlobalRec(nullptr), GlobalSFun(nullptr), StringArena(&StringRegion),
        ParseArena(&ParseRegion), DefArena(&DefRegion)
  { }

  inline SExpr* global() { return GlobalSFun; }

  // Add Defs to the set of global, newly parsed definitions.
  void addDefinitions(std::vector<SExpr*> &Defs);

  // Lower the parsed definitions.
  void lower();

  // Dump outputs to the given stream
  void print(std::ostream &SS);

private:
  MemRegion StringRegion;  // Region to hold string constants.
  MemRegion ParseRegion;   // Region for the initial AST produced by the parser.
  MemRegion DefRegion;     // Region for rewritten definitions.

  Record    *GlobalRec;
  SFunction *GlobalSFun;

public:
  MemRegionRef StringArena;
  MemRegionRef ParseArena;
  MemRegionRef DefArena;
};


}  // end namespace ohmu

#endif  // OHMU_TIL_GLOBAL_H
