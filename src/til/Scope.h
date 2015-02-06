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

#ifndef OHMU_TIL_SCOPE_H
#define OHMU_TIL_SCOPE_H

#include "TIL.h"

#include <cstddef>
#include <memory>
#include <vector>


namespace ohmu {
namespace til  {


/// A ScopeFrame maintains information about how variables in one lexical
/// scope are mapped to expressions in another lexical scope.  This includes:
/// (1) Bindings for all variables (i.e. function parameters).
/// (2) Bindings for all instructions (which are essentially let-variables.)
/// (3) Bindings for all blocks (which are essentially function letrecs.)
class ScopeFrame {
public:
  struct SubstitutionEntry {
    SubstitutionEntry(VarDecl* Vd, SExpr *S) : VDecl(Vd), Subst(S) { }

    VarDecl* VDecl;
    SExpr*   Subst;
  };

  /// Return the binding for the i^th variable (VarDecl) from the top of the
  /// current scope.
  SExpr* var(unsigned i) const {
    assert(i < VarMap.size() && "Variable index out of bounds.");
    return VarMap[VarMap.size()-i-1].Subst;
  }

  /// Set the binding for the i^th variable (VarDecl) from top of scope.
  void setVar(unsigned i, SExpr *E) {
    VarMap[VarMap.size()-i-1].Subst = E;
  }

  /// Return the variable substitution for Orig.
  SExpr* lookupVar(VarDecl *Orig) { return var(Orig->varIndex()); }

  /// Return the number of variables (VarDecls) in the current scope.
  unsigned numVars() { return VarMap.size()-1; }

  /// Return the VarDecl->SExpr map entry for the i^th variable.
  SubstitutionEntry& entry(unsigned i) {
    return VarMap[VarMap.size()-i-1];
  }

  /// Return whatever the given instruction maps to during CFG rewriting.
  SExpr* lookupInstr(Instruction *Orig) {
    return InstructionMap[Orig->instrID()];
  }

  /// Return whatever the given block maps to during CFG rewriting.
  BasicBlock* lookupBlock(BasicBlock *Orig) {
    return BlockMap[Orig->blockID()];
  }

  // Create and return a fresh DeBruin index for a newly-allocated VarDecl.
  // Should be called in conjunction with enterScope.
  unsigned allocVarIndex() { return DeBruin++; }

  // Free the DeBruijn index.
  // Should be called in conjunction with exitScope.
  void freeVarIndex() { --DeBruin; }

  /// Enter a function scope (or apply a function), by mapping Orig -> E.
  void enterScope(VarDecl *Orig, SExpr *E) {
    // Assign indices to variables if they haven't been assigned yet.
    if (Orig->varIndex() == 0)
      Orig->setVarIndex(VarMap.size());
    else
      assert(Orig->varIndex() == VarMap.size() && "De Bruijn index mismatch.");
    VarMap.push_back(SubstitutionEntry(Orig, E));
  }

  /// Exit a function scope.
  void exitScope(VarDecl *Orig) {
    assert(Orig->varIndex() == VarMap.size()-1 && "Unmatched scopes.");
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

  ScopeFrame() : DeBruin(1) {
    // Variable ID 0 means uninitialized.
    VarMap.push_back(SubstitutionEntry(nullptr, nullptr));
  }

private:
  ScopeFrame(const ScopeFrame &F)
      : DeBruin(F.DeBruin), VarMap(F.VarMap), InstructionMap(F.InstructionMap),
        BlockMap(F.BlockMap) { }

  unsigned                       DeBruin;         //< current debruin index
  std::vector<SubstitutionEntry> VarMap;          //< map vars to values
  std::vector<SExpr*>            InstructionMap;  //< map instrs to values
  std::vector<BasicBlock*>       BlockMap;        //< map blocks to new blocks
};


typedef std::unique_ptr<ScopeFrame> UniqueScope;


}  // end namespace til
}  // end namespace ohmu


#endif  // SRC_TIL_SCOPEHANDLER_H_
