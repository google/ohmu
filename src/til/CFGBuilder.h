//===- CFGBuilder.h --------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//


#include "TIL.h"
#include "TILTraverse.h"
#include "TILPrettyPrint.h"

#include <vector>


#ifndef OHMU_TIL_CFGBUILDER_H
#define OHMU_TIL_CFGBUILDER_H

namespace ohmu {
namespace til  {


/// This class provides a useful interface for building and rewriting CFGs.
/// It maintains information about the lexical context in which a term is
/// being created, such as the current CFG and the current block.
class CFGBuilder {
public:
  // Lightweight struct that summarizes info about the current context.
  // Used to quickly switch output contexts during lazy rewriting.
  struct BuilderState {
    BuilderState() : DeBruin(1), EmitInstrs(false) { }
    BuilderState(unsigned Db, bool Em) : DeBruin(Db), EmitInstrs(Em) { }

    unsigned DeBruin;     // DeBruin index for current location.
    bool     EmitInstrs;  // Do we have a current CFG?
  };

  void setArena(MemRegionRef A) { Arena = A; }

  /// Return the memory pool used by this builder to create new instructions.
  MemRegionRef& arena() { return Arena; }

  /// Return the diagnostic emitter used by this builder.
  DiagnosticEmitter& diag()  { return Diag; }

  /// Return the current CFG being constructed (if any)
  SCFG* currentCFG() { return CurrentCFG; }

  /// Return the current basic block being constructed (if any)
  BasicBlock* currentBB() { return CurrentBB;  }

  /// Return true if we are in a CFG, and emitting instructions.
  bool emitInstrs() { return CurrentState.EmitInstrs; }

  /// Return the current deBruin index().  (Index of last variable in scope).
  unsigned deBruinIndex() { return CurrentState.DeBruin; }

  /// Return the deBruin index of the first argument to the enclosing
  /// nested function.  (For functions which are nested inside a CFG.)
  unsigned deBruinIndexOfEnclosingNestedFunction() {
    return OldCfgState.DeBruin;
  }

  /// Return the current builder state.
  const BuilderState currentState() { return CurrentState; }

  /// Switch to a new builder state.
  BuilderState switchState(const BuilderState &S) {
    assert(S.EmitInstrs == false && "Cannot switch into an emitting state.");
    auto Temp = CurrentState;  CurrentState = S;  return Temp;
  }

  /// Restore the previous builder state (returned from currentState)
  void restoreState(const BuilderState &S) {
    CurrentState = S;
  }

  /// Switch builder state to stop emitting instructions to the current CFG.
  bool disableEmit() {
    bool Temp = CurrentState.EmitInstrs;
    CurrentState.EmitInstrs = false;
    return Temp;
  }

  /// Restore the previous emit flag.
  void restoreEmit(bool B) { CurrentState.EmitInstrs = B; }

  /// Enter the scope of Nvd.
  void enterScope(VarDecl *Nvd);

  /// Exit the scope of the topmost variable.
  void exitScope();

  /// Start working on the given CFG.
  /// If Cfg is null, then create a new one.
  /// If Cfg is not null, then NumBlocks and NumInstrs are ignored.
  virtual SCFG* beginCFG(SCFG *Cfg, unsigned NumBlocks = 0,
                                    unsigned NumInstrs = 0);

  /// Finish working on the current CFG.
  virtual void endCFG();

  /// Start working on the given basic block.
  /// If Overwrite is true, any existing instructions will marked as "removed"
  /// from the block.  They will not actually be removed until endBlock() is
  /// is called, so in-place rewriting passes can still traverse them.
  virtual void beginBlock(BasicBlock *B, bool Overwrite = false);

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
  Record* newRecord(unsigned NSlots = 0, SExpr* Parent = nullptr) {
    return new (Arena) Record(Arena, NSlots, Parent);
  }

  ScalarType* newScalarType(BaseType Bt) {
    return new (Arena) ScalarType(Bt);
  }
  Literal* newLiteralVoid() {
    return new (Arena) Literal(BaseType::getBaseType<void>());
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
  SExpr* newIfThenElse(SExpr* C, SExpr* T, SExpr* E) {
    return new (Arena) IfThenElse(C, T, E);
  }
  SExpr* newIdentifier(StringRef S) {
    return new (Arena) Identifier(S);
  }

  template<typename AnnType, typename... Params>
  AnnType* newAnnotationT(Params... Ps) {
    return new (Arena) AnnType(Ps...);
  }

  /// Create a new basic block in the current cfg.
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
  void setPhiArgument(Phi* Ph, SExpr* E, unsigned Idx);


  CFGBuilder()
    : CurrentCFG(nullptr), CurrentBB(nullptr), OverwriteCurrentBB(false),
      OldCfgState(0, false)
  { }
  CFGBuilder(MemRegionRef A, bool Inplace = false)
    : Arena(A), CurrentCFG(nullptr), CurrentBB(nullptr),
      OverwriteCurrentBB(false), OldCfgState(0, false)
  { }
  virtual ~CFGBuilder() { }

protected:
  MemRegionRef               Arena;          ///< pool to create new instrs
  SCFG*                      CurrentCFG;     ///< current CFG
  BasicBlock*                CurrentBB;      ///< current basic block
  std::vector<Phi*>          CurrentArgs;    ///< arguments in CurrentBB.
  std::vector<Instruction*>  CurrentInstrs;  ///< instructions in CurrentBB.
  bool                       OverwriteCurrentBB;

  BuilderState               CurrentState;   ///< state at current location.
  BuilderState               OldCfgState;    ///< state at old CFG location.

  DiagnosticEmitter Diag;
};


template<class T>
inline T* CFGBuilder::addInstr(T* I) {
  if (!I || !CurrentState.EmitInstrs)
    return I;
  assert(!I->block() && "Instruction was already added to a block.");
  I->setBlock(CurrentBB);        // Mark I as having been added.
  CurrentInstrs.push_back(I);
  return I;
}

inline Phi* CFGBuilder::addArg(Phi* A) {
  if (!A || !CurrentState.EmitInstrs)
    return A;
  assert(!A->block() && "Argument was already added to a block.");
  A->setBlock(CurrentBB);        // Mark A as having been added
  CurrentArgs.push_back(A);
  return A;
}


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_CFGBUILDER_H_
