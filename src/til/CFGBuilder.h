//===- CFGBuilder.h --------------------------------------------*- C++ --*-===//
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


#include "DiagnosticEmitter.h"
#include "TIL.h"
#include "TILTraverse.h"
#include "TILPrettyPrint.h"


#include <vector>


#ifndef OHMU_TIL_CFGBUILDER_H_
#define OHMU_TIL_CFGBUILDER_H_

namespace ohmu {
namespace til  {


/// This class provides a useful interface for building and rewriting CFGs.
class CFGBuilder {
public:
  void setArena(MemRegionRef A) { Arena = A; }

  MemRegionRef& arena() { return Arena; }

  SCFG*        currentCFG() { return CurrentCFG; }
  BasicBlock*  currentBB()  { return CurrentBB;  }

  /// Set the emitInstrs flag, and return old flag.
  /// If b is true, then the builder will add instructions to the current CFG.
  bool switchEmit(bool b) {
    bool ob = EmitInstrs;
    EmitInstrs = b;
    return ob;
  }

  /// Restore the emitInstrs flag.
  void restoreEmit(bool b) { EmitInstrs = b; }

  /// Start working on the given CFG.
  /// If Cfg is null, then create a new one.
  /// If Cfg is not null, then NumBlocks and NumInstrs are ignored.
  virtual SCFG* beginCFG(SCFG *Cfg, unsigned NumBlocks = 0,
                                    unsigned NumInstrs = 0);

  /// Finish working on the current CFG.
  virtual void endCFG();

  /// Start working on the given basic block.
  virtual void beginBlock(BasicBlock *B);

  /// Finish working on the current basic block.
  virtual void endBlock(Terminator *Term);


  VarDecl* newVarDecl(VarDecl::VariableKind K, StringRef S, SExpr* E) {
    return new (Arena) VarDecl(K, S, E);
  }
  Function* newFunction(VarDecl *Nvd, SExpr* E0) {
    return new (Arena) Function(Nvd, E0);
  }
  Code* newCode(SExpr* E0, SExpr* E1) {
    return new (Arena) Code(E0, E1);
  }
  Field* newField(SExpr* E0, SExpr* E1) {
    return new (Arena) Field(E0, E1);
  }
  Slot* newSlot(StringRef S, SExpr *E0) {
    return new (Arena) Slot(S, E0);
  }
  Record* newRecord(unsigned NSlots = 0) {
    return new (Arena) Record(Arena, NSlots);
  }

  Literal* newLiteralVoid() {
    return new (Arena) Literal(ValueType::getValueType<void>());
  }
  template<class T>
  LiteralT<T>* newLiteralT(T Val) {
    return new (Arena) LiteralT<T>(Val);
  }
  Variable* newVariable(VarDecl* Vd) {
    return new (Arena) Variable(Vd);
  }
  Apply* newApply(SExpr* E0, SExpr* E1, Apply::ApplyKind K=Apply::FAK_Apply) {
    return new (Arena) Apply(E0, E1, K);
  }
  Project* newProject(SExpr* E0, StringRef S) {
    return new (Arena) Project(E0, S);
  }

  Call* newCall(SExpr* E0) {
    return addInstr(new (Arena) Call(E0));
  }
  Alloc* newAlloc(SExpr* E0, Alloc::AllocKind K) {
    return addInstr(new (Arena) Alloc(E0, K));
  }
  Load* newLoad(SExpr* E0) {
    return addInstr(new (Arena) Load(E0));
  }
  Store* newStore(SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) Store(E0, E1));
  }
  ArrayIndex* newArrayIndex(SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) ArrayIndex(E0, E1));
  }
  ArrayAdd* newArrayAdd(SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) ArrayAdd(E0, E1));
  }
  UnaryOp* newUnaryOp(TIL_UnaryOpcode Op, SExpr* E0) {
    return addInstr(new (Arena) UnaryOp(Op, E0));
  }
  BinaryOp* newBinaryOp(TIL_BinaryOpcode Op, SExpr* E0, SExpr* E1) {
    return addInstr(new (Arena) BinaryOp(Op, E0, E1));
  }
  Cast* newCast(TIL_CastOpcode Op, SExpr* E0) {
    return addInstr(new (Arena) Cast(Op, E0));
  }

  /// Terminate the current block with a branch instruction.
  /// If B0 and B1 are not specified, then this will create new blocks.
  Branch* newBranch(SExpr *Cond, BasicBlock *B0 = nullptr,
                                 BasicBlock *B1 = nullptr);

  /// Terminate the current block with a Goto instruction.
  /// If result is specified, then passes result as an argument.
  Goto* newGoto(BasicBlock *B, SExpr* Result = nullptr);

  /// Terminate the current block with a Goto instruction.
  /// Passes args as arguments.
  Goto* newGoto(BasicBlock *B, ArrayRef<SExpr*> Args);

  /// Terminate the current block with a Return instruction.
  Return* newReturn(SExpr* E) {
    auto* Res = new (Arena) Return(E);
    endBlock(Res);
    return Res;
  }

  SExpr* newUndefined() {
    return new (Arena) Undefined();
  }
  SExpr* newWildcard() {
    return new (Arena) Wildcard();
  }
  SExpr* newLet(VarDecl *Nvd, SExpr* B) {
    return new (Arena) Let(Nvd, B);
  }
  SExpr* newLetrec(VarDecl *Nvd, SExpr* B) {
    return new (Arena) Letrec(Nvd, B);
  }
  SExpr* newIfThenElse(SExpr* C, SExpr* T, SExpr* E) {
    return new (Arena) IfThenElse(C, T, E);
  }

  /// Create a new basic block.
  /// If Nargs > 0, will create new Phi nodes for arguments.
  /// If NPreds > 0, will reserve space for predecessors.
  BasicBlock* newBlock(unsigned Nargs = 0, unsigned NPreds = 0);


  /// Add I to the current basic basic block.
  template<class T> inline T* addInstr(T* I);

  // Add A to the current basic block.
  // Note that arguments (Phi nodes) are usually created by newBlock(),
  // rather than being added manually.
  inline Phi* addArg(Phi* A);

  /// Utility function for rewriting phi nodes.
  /// Implementation of handlePhiArg used by CopyReducer and InplaceReducer.
  void rewritePhiArg(SExpr *Ne, Goto *NG, SExpr *Res);

  CFGBuilder()
    : OverwriteArguments(false), OverwriteInstructions(false),
      CurrentCFG(nullptr), CurrentBB(nullptr), EmitInstrs(true)
  { }
  CFGBuilder(MemRegionRef A)
    : Arena(A), OverwriteArguments(false), OverwriteInstructions(false),
      CurrentCFG(nullptr), CurrentBB(nullptr), EmitInstrs(true)
  { }
  virtual ~CFGBuilder() { }

private:
  void setPhiArgument(Phi* Ph, SExpr* E, unsigned Idx);

protected:
  MemRegionRef               Arena;
  bool OverwriteArguments;     //< Set to true for passes which rewrite Phi.
  bool OverwriteInstructions;  //< Set to true for in-place rewriting passes.

  SCFG*                      CurrentCFG;
  BasicBlock*                CurrentBB;
  std::vector<Phi*>          CurrentArgs;     //< arguments in CurrentBB.
  std::vector<Instruction*>  CurrentInstrs;   //< instructions in CurrentBB.
  bool                       EmitInstrs;      //< should we emit instrs?

  DiagnosticEmitter diag;
};


template<class T>
inline T* CFGBuilder::addInstr(T* I) {
  if (!I || !EmitInstrs)
    return I;

  if (I->block() == nullptr)
    I->setBlock(CurrentBB);        // Mark I as having been added.
  assert(I->block() == CurrentBB);
  CurrentInstrs.push_back(I);
  return I;
}

inline Phi* CFGBuilder::addArg(Phi* A) {
  if (!A || !EmitInstrs)
    return A;

  if (A->block() == nullptr)
    A->setBlock(CurrentBB);      // Mark A as having been added
  assert(A->block() == CurrentBB && "Invalid argument.");
  CurrentArgs.push_back(A);
  return A;
}


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_CFGBUILDER_H_
