//===- Scope.h -------------------------------------------------*- C++ --*-===//
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

#include "TIL.h"

#include <cstddef>
#include <memory>
#include <vector>


namespace ohmu {
namespace til  {


/// A ScopeFrame maintains information about how the lexical scope of a term
/// is mapped to some other scope during rewriting or inlining.
/// Lexical scope includes:
/// (1) Bindings for all variables (i.e. function parameters).
/// (2) Bindings for all instructions (which are essentially let-variables.)
/// (3) Bindings for all blocks (which are essentially function letrecs.)
class ScopeFrame {
public:
  ScopeFrame() : OrigCFG(nullptr) {
    // Variable ID 0 means uninitialized.
    VarMap.push_back(nullptr);
  }

  /// During alpha-renaming (making a copy of a function), the original VarDecl
  /// (function parameter) is mapped to a new VarDecl.
  /// During inlining, an SExpr is substituted for each variable, so the
  /// VarDecl of the variable maps to its substitution.
  SExpr* lookupVar(VarDecl *Orig) {
    return VarMap[Orig->varIndex()];
  }

  /// Return the binding for the i^th variable (VarDecl) from the top of the
  /// current scope.
  SExpr* var(unsigned i) const { return VarMap[VarMap.size() - i - 1];  }

  /// Set the binding for the i^th variable (VarDecl) from top of scope.
  void setVar(unsigned i, SExpr *E) { VarMap[VarMap.size() - i - 1] = E; }

  /// Return the binding for the i^th variable (VarDecl) from the top of the
  /// current scope, or null if it does not map to another VarDecl.
  VarDecl* varDecl(unsigned i) { return dyn_cast_or_null<VarDecl>(var(i)); }

  /// Return the number of variables (VarDecls) in the current scope.
  unsigned numVars() { return VarMap.size(); }

  /// Return whatever the given instruction maps to during CFG rewriting.
  SExpr* lookupInstr(Instruction *Orig) {
    return InstructionMap[Orig->instrID()];
  }

  /// Return whatever the given block maps to during CFG rewriting.
  BasicBlock* lookupBlock(BasicBlock *Orig) {
    return BlockMap[Orig->blockID()];
  }

  /// Enter a function scope (or apply a function), by mapping Orig -> E.
  void enterScope(VarDecl *Orig, SExpr *E) {
    if (Orig->varIndex() == 0)
      Orig->setVarIndex(VarMap.size());
    else
      assert(Orig->varIndex() == VarMap.size() && "Invalid numbering.");
    VarMap.push_back(E);
  }

  /// Exit a function scope.
  void exitScope(VarDecl *Orig) {
    if (Orig->varIndex() == 0)
      return;
    assert(Orig->varIndex() == VarMap.size()-1 && "Traversal Error.");
    VarMap.pop_back();
  }

  /// Enter a CFG, which will initialize the instruction and block maps.
  void enterCFG(SCFG *Orig, SCFG *S);

  /// Exit the CFG, which will clear the maps.
  void exitCFG();

  /// Add a new instruction to the map.
  void updateInstructionMap(Instruction *Orig, SExpr *E) {
    if (Orig->instrID() > 0)
      InstructionMap[Orig->instrID()] = E;
  }

  /// Map Orig to B, and map its arguments to the arguments of B.
  void updateBlockMap(BasicBlock *Orig, BasicBlock *B);

  /// Create a copy of this scope.  (Used for lazy rewriting)
  ScopeFrame* clone() { return new ScopeFrame(*this); }


private:
  ScopeFrame(const ScopeFrame &F)
      : OrigCFG(F.OrigCFG), VarMap(F.VarMap), InstructionMap(F.InstructionMap),
        BlockMap(F.BlockMap) { }

  SCFG*                     OrigCFG;         //< ptr to CFG being rewritten.
  std::vector<SExpr*>       VarMap;          //< map vars to values
  std::vector<SExpr*>       InstructionMap;  //< map instrs to values
  std::vector<BasicBlock*>  BlockMap;        //< map blocks to new blocks
};



class ScopeHandler {
public:
  ScopeHandler() : Scope(new ScopeFrame()) { }

  /// Enter the lexical scope of Orig, which is rewritten to Nvd.
  void enterScope(VarDecl* Orig, VarDecl* Nvd);

  /// Exit the lexical scope of Orig.
  void exitScope(VarDecl* Orig);

  ScopeFrame& scope() { return *Scope; }

public:
  std::unique_ptr<ScopeFrame> Scope;
};


}  // end namespace til
}  // end namespace ohmu


#endif  // SRC_TIL_SCOPEHANDLER_H_
