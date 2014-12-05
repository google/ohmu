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

#include "Global.h"
#include "CFGReducer.h"

namespace ohmu {
namespace til  {


template<class T>
inline Slot* scalarTypeSlot(Global &G, StringRef Name) {
  auto* Ty = new (G.LangArena) ScalarType(BaseType::getBaseType<T>());
  auto* Slt = new (G.ParseArena) Slot(Name, Ty);
  Slt->setModifier(Slot::SLT_Final);
  return Slt;
}

void Global::createPrelude() {
  PreludeDefs.push_back( scalarTypeSlot<void>     (*this, "Void")   );
  PreludeDefs.push_back( scalarTypeSlot<bool>     (*this, "Bool")   );
  PreludeDefs.push_back( scalarTypeSlot<int8_t>   (*this, "Int8")   );
  PreludeDefs.push_back( scalarTypeSlot<uint8_t>  (*this, "UInt8")  );
  PreludeDefs.push_back( scalarTypeSlot<int16_t>  (*this, "Int16")  );
  PreludeDefs.push_back( scalarTypeSlot<uint16_t> (*this, "UInt16") );
  PreludeDefs.push_back( scalarTypeSlot<int32_t>  (*this, "Int32")  );
  PreludeDefs.push_back( scalarTypeSlot<uint32_t> (*this, "UInt32") );
  PreludeDefs.push_back( scalarTypeSlot<int64_t>  (*this, "Int64")  );
  PreludeDefs.push_back( scalarTypeSlot<uint64_t> (*this, "UInt64") );
  PreludeDefs.push_back( scalarTypeSlot<float>    (*this, "Float")  );
  PreludeDefs.push_back( scalarTypeSlot<double>   (*this, "Double") );
  PreludeDefs.push_back( scalarTypeSlot<StringRef>(*this, "String") );
  PreludeDefs.push_back( scalarTypeSlot<void*>    (*this, "PointerType") );

  PreludeDefs.push_back( scalarTypeSlot<int32_t>(*this, "Int")  );
  PreludeDefs.push_back( scalarTypeSlot<int32_t>(*this, "UInt") );
}


void Global::addDefinitions(std::vector<SExpr*>& Defs) {
  assert(GlobalRec == nullptr && "FIXME: support multiple calls.");

  if (PreludeDefs.empty())
    createPrelude();

  unsigned Sz = PreludeDefs.size() + Defs.size();
  GlobalRec = new (ParseArena) Record(ParseArena, Sz);

  for (auto *Slt : PreludeDefs) {
    GlobalRec->slots().emplace_back(ParseArena, Slt);
  }
  for (auto *E : Defs) {
    auto *Slt = dyn_cast_or_null<Slot>(E);
    if (Slt)
      GlobalRec->slots().emplace_back(ParseArena, Slt);
  }

  auto *Vd = new (ParseArena) VarDecl(VarDecl::VK_SFun, "global", nullptr);
  GlobalSFun = new (ParseArena) Function(Vd, GlobalRec);
}


void Global::lower() {
  SExpr* E = CFGReducer::lower(GlobalSFun, DefArena);

  // Replace the global definitions with lowered versions.
  GlobalSFun = dyn_cast<Function>(E);
  if (GlobalSFun)
    GlobalRec = dyn_cast<Record>(GlobalSFun->body());
  else
    GlobalRec = nullptr;
}


void Global::print(std::ostream &SS) {
  TILDebugPrinter::print(GlobalSFun, SS);
}


}  // end namespace til
}  // end namespace ohmu
