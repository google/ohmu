//===- VarContext.h --------------------------------------------*- C++ --*-===//
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

#ifndef OHMU_TIL_SCOPEHANDLER_H
#define OHMU_TIL_SCOPEHANDLER_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"

#include <cstddef>
#include <memory>
#include <vector>


namespace ohmu {

using namespace clang::threadSafety::til;


class VarContext {
public:
  VarContext() {
    // ID zero is reserved for unnumbered variables.
    VarDeclMap.push_back(nullptr);
  }

  VarDecl*&  operator[](unsigned i) {
    assert(i < size() && "Array out of bounds.");
    return VarDeclMap[size()-1-i];
  }

  /// Look up a variable by name.
  VarDecl* lookup(StringRef s) const;

  /// Look up a VarDecl by index.
  VarDecl* map(unsigned i) const { return VarDeclMap[i]; }

  size_t size() const      { return VarDeclMap.size(); }
  void   push(VarDecl *vd) { VarDeclMap.push_back(vd); }
  void   pop()             { VarDeclMap.pop_back();    }

  VarContext* clone()  { return new VarContext(*this); }

private:
  VarContext(const VarContext& ctx) : VarDeclMap(ctx.VarDeclMap) { }

  std::vector<VarDecl*> VarDeclMap;  //< Map from old to new VarDecls.
};


class ScopeHandler {
public:
  ScopeHandler() : VarCtx(new VarContext()) { }

  /// Enter the lexical scope of Orig, which is rewritten to Nvd.
  void enterScope(VarDecl* Orig, VarDecl* Nvd);

  /// Exit the lexical scope of Orig.
  void exitScope(VarDecl* Orig);

  VarContext& varCtx() { return *VarCtx; }

protected:
  std::unique_ptr<VarContext> VarCtx;
};



}  // end namespace ohmu


#endif  // SRC_TIL_SCOPEHANDLER_H_
