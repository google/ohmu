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


#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyPrint.h"


#ifndef OHMU_TIL_CFGBUILDER_H_
#define OHMU_TIL_CFGBUILDER_H_

namespace ohmu {

using namespace clang::threadSafety::til;


/// This class provides a useful interface for building and rewriting CFGs.
class CFGBuilder {
public:
  void setArena(MemRegionRef A) { Arena = A; }

  SCFG*       currentCFG() { return CurrentCFG; }
  BasicBlock* currentBB()  { return CurrentBB;  }

  /// Start working on the given CFG.
  /// If Cfg is null, then create a new one.
  /// If Cfg is not null, then NumBlocks and NumInstrs are ignored.
  virtual SCFG* beginSCFG(SCFG *Cfg, unsigned NumBlocks = 0,
                                     unsigned NumInstrs = 0);

  /// Finish working on the current CFG.
  virtual void endSCFG();

  /// Start working on the given basic block.
  virtual void beginBlock(BasicBlock *B);

  /// Finish working on the current basic block.
  virtual void endBlock(Terminator *Term);

  /// Map B->B2 in BlockMap, and map their arguments in InstructionMap.
  virtual void mapBlock(BasicBlock *B, BasicBlock *B2);

  /// Handle futures that are inserted into the instruction stream.
  virtual void handleFutureInstr(Instruction** Iptr, Future* F) { }

  /// Handle futures that are inserted into Phi arguments.
  virtual void handleFuturePhiArg(SExpr** Eptr, Future *F) { }

  /// Add I to the current basic basic block.
  template<class T> inline T* addInstr(T* I);

  // Add A to the current basic block.
  // Note that arguments (Phi nodes) are usually created by newBlock(),
  // rather than being added manually.
  inline Phi* addArg(Phi* A);

  /// Create a new basic block.
  /// If Nargs > 0, will create new Phi nodes for arguments.
  /// If NPreds > 0, will reserve space for predecessors.
  BasicBlock* newBlock(unsigned Nargs = 0, unsigned NPreds = 0);

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

  /// Utility function for rewriting phi nodes.
  /// Implementation of handlePhiArg used by CopyReducer and InplaceReducer.
  void rewritePhiArg(Phi &Orig, Goto *NG, SExpr *Res);

  CFGBuilder()
    : OverwriteArguments(false), OverwriteInstructions(false),
      CurrentCFG(nullptr), CurrentBB(nullptr)
  { }
  CFGBuilder(MemRegionRef A)
    : Arena(A), OverwriteArguments(false), OverwriteInstructions(false),
      CurrentCFG(nullptr), CurrentBB(nullptr)
  { }
  virtual ~CFGBuilder() { }

protected:
  MemRegionRef               Arena;
  bool OverwriteArguments;     //< Set to true for passes which rewrite Phi.
  bool OverwriteInstructions;  //< Set to true for in-place rewriting passes.

  SCFG*                      CurrentCFG;
  BasicBlock*                CurrentBB;
  std::vector<Phi*>          CurrentArgs;     //< arguments in CurrentBB.
  std::vector<Instruction*>  CurrentInstrs;   //< instructions in CurrentBB.

  std::vector<SExpr*>        InstructionMap;  //< map old to new instrs
  std::vector<BasicBlock*>   BlockMap;        //< map old to new blocks
};


template<class T>
inline T* CFGBuilder::addInstr(T* I) {
  if (!I)
    return nullptr;

  if (I->block() == nullptr)
    I->setBlock(CurrentBB);        // Mark I as having been added.
  assert(I->block() == CurrentBB);
  CurrentInstrs.push_back(I);
  return I;
}

inline Phi* CFGBuilder::addArg(Phi* A) {
  if (!A)
    return nullptr;

  if (A->block() == nullptr)
    A->setBlock(CurrentBB);      // Mark A as having been added
  assert(A->block() == CurrentBB && "Invalid argument.");
  CurrentArgs.push_back(A);
  return A;
}


}

#endif  // OHMU_TIL_CFGBUILDER_H_
