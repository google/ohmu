//===- VarContext.cpp ------------------------------------------*- C++ --*-===//
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


#include "til/VarContext.h"

namespace ohmu {

using namespace clang::threadSafety::til;


VarDecl* VarContext::lookup(StringRef s) const {
  for (unsigned i=0,n=VarDeclMap.size(); i < n; ++i) {
    VarDecl* vd = VarDeclMap[n-i-1];
    if (vd && vd->name() == s) {
      return vd;
    }
  }
  return nullptr;
}


void ScopeHandler::enterScope(VarDecl *orig, VarDecl *nv) {
  // VarDecls are initially unnumbered, so assign indexes if need be.
  if (orig->varIndex() == 0) {
    // Skip unnamed, unnumbered let variables.
    if (orig->kind() == VarDecl::VK_Let && orig->name().length() == 0)
      return;
    orig->setVarIndex(VarCtx->size());
  }
  else {
    // Numberings should be consecutive.
    assert(orig->varIndex() == VarCtx->size() && "Invalid Var Numbering.");
  };

  VarCtx->push(nv);

  // Copy names of let-variables to their definitions.
  if (nv->kind() == VarDecl::VK_Let && nv->definition()) {
    if (Instruction *I = dyn_cast<Instruction>(nv->definition()))
      if (I->name().length() == 0)
        I->setName(nv->name());
  }
}


void ScopeHandler::exitScope(VarDecl *orig) {
  if (orig->varIndex() == 0)
    return;
  assert(orig->varIndex() == VarCtx->size()-1 && "Unmatched enter/exit scope");
  VarCtx->pop();
}


}  // end namespace ohmu

