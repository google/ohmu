//===- Global.cpp ----------------------------------------------*- C++ --*-===//
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


#include "til/Global.h"
#include "til/CFGReducer.h"


namespace ohmu {

using namespace clang::threadSafety::til;


void Global::addDefinitions(std::vector<SExpr*>& Defs) {
  assert(GlobalRec == nullptr && "FIXME: support multiple calls.");

  GlobalRec = new (ParseArena) Record(ParseArena, Defs.size());
  for (auto *E : Defs) {
    auto *Slt = dyn_cast_or_null<Slot>(E);
    if (Slt)
      GlobalRec->slots().push_back(Slt);
  }
  auto *Vd = new (ParseArena) VarDecl("global", nullptr);
  GlobalSFun = new (ParseArena) SFunction(Vd, GlobalRec);
}


void Global::lower() {
  SExpr* E = CFGReducer::lower(GlobalSFun, DefArena);

  // Replace the global definitions with lowered versions.
  GlobalSFun = dyn_cast<SFunction>(E);
  if (GlobalSFun)
    GlobalRec = dyn_cast<Record>(GlobalSFun->body());
  else
    GlobalRec = nullptr;
}


void Global::print(std::ostream &SS) {
  TILDebugPrinter::print(GlobalSFun, SS);
}


}  // end namespace ohmu
