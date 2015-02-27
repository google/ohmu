//===- AttributeGrammar.h --------------------------------------*- C++ --*-===//
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
//
// AttributeGrammar.h defines several helper classes that extend the TIL
// traversal system to support attribute-grammar style computations.
//
/// An attribute grammar traverses the AST, and computes attributes for each
/// term.  An attribute captures some information about the term, such as its
/// type.  There are two kinds of attribute, -Synthesized-, and -Inherited-.
///
/// Synthesized attributes are computed for each term from the attributes of
/// its children.  They are typically used to compute a result for the
/// traversal.  Examples include:
///   * A copy of the term.
///   * A rewrite or simplification of the term.
///   * The type of a term.
///   * Some other analysis result for a term.
///
/// Inherited attributes propagate information from parent to child during the
/// traversal, and are typically used to represent the lexical scope, e.g.
///   * The typing context (i.e. the names and types of local variables)
///   * The current continuation, for a CPS transform.
///
/// We implement attribute grammars by doing a depth first traversal of the
/// AST.  Synthesized attributes are stored in a stack, which mirrors the call
/// stack of the traversal.  Inherited attributes are stored in a Scope object,
/// which is destructively updated via enter/exit calls.
///
/// Note that when doing rewriting, there are actually two contexts.
/// The source context holds information about the term that is being
/// traversed, and the destination context holds information about the
/// term that is being produced.  The classes here only maintains information
/// about the source context; the destination context is maintained by
/// CFGBuilder.
///
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_ATTRIBUTEGRAMMAR_H
#define OHMU_TIL_ATTRIBUTEGRAMMAR_H

#include "TIL.h"
#include "TILTraverse.h"

namespace ohmu {
namespace til {



/// AttrBase defines the basic interface expected for synthesized attributes.
class AttrBase {
  // Create an empty attribute
  AttrBase() { }

  AttrBase& operator=(AttrBase&& A) = default;

private:
  AttrBase(const AttrBase& A) = default;
};



/// A ScopeFrame maps variables in the lexical context to synthesized
/// attributes.  In particular, it tracks attributes for:
/// (1) Ordinary variables (i.e. function parameters).
/// (2) Instructions (which are essentially let-variables.)
template<class Attr, typename ExprStateT=int>
class ScopeFrame {
public:
  struct VarMapEntry {
    VarMapEntry(VarDecl* Vd, Attr &&Va)
        : VDecl(Vd), VarAttr(std::move(Va))
    { }

    VarDecl* VDecl;     // VarDecl in original term
    Attr     VarAttr;   // The attribute associated with the VarDecl.
  };

public:
  /// Change state to enter a sub-expression.
  /// Subclasses should override it.
  ExprStateT enterSubExpr(TraversalKind K) { return ExprStateT(); }

  /// Restore state when exiting a sub-expression.
  /// Subclasses should override it.
  void exitSubExpr(TraversalKind K, ExprStateT S) { }

  /// Return the binding for the i^th variable (VarDecl) in the scope
  /// Note that var(0) is reserved; 0 denotes an undefined variable.
  Attr& var(unsigned i) { return VarMap[i].VarAttr; }

  /// Set the binding for the i^th variable (VarDecl) from top of scope.
  /// Note that var(0) is reserved.
  void setVar(unsigned i, Attr&& At) { VarMap[i].VarAttr = std::move(At); }

  /// Return the variable substitution for Orig.
  Attr& lookupVar(VarDecl *Orig) { return var(Orig->varIndex()); }

  /// Return the number of variables (VarDecls) in the current scope.
  /// Note that this will always be > 0, since var(0) is reserved.
  unsigned numVars() { return VarMap.size(); }

  /// Return the VarDecl->SExpr map entry for the i^th variable.
  VarMapEntry& entry(unsigned i) { return VarMap[i]; }

  /// Return whatever the given instruction maps to during CFG rewriting.
  Attr& lookupInstr(Instruction *Orig) {
    return InstructionMap[Orig->instrID()];
  }

  /// Enter a function scope (or apply a function), by mapping Orig -> E.
  void enterScope(VarDecl *Orig, Attr&& At) {
    // Assign indices to variables if they haven't been assigned yet.
    if (Orig) {
      // TODO: FIXME!  Orig should always be specified.
      if (Orig->varIndex() == 0)
        Orig->setVarIndex(VarMap.size());
      else
        assert(Orig->varIndex() == VarMap.size() && "De Bruijn index mismatch.");
    }
    VarMap.push_back(VarMapEntry(Orig, std::move(At)));
  }

  /// Exit a function scope.
  void exitScope() {
    VarMap.pop_back();
  }

  /// Enter a CFG, which will initialize the instruction and block maps.
  void enterCFG(SCFG *Orig) {
    assert(InstructionMap.size() == 0 && "No support for nested CFGs");
    InstructionMap.resize(Orig->numInstructions());
  }

  /// Exit the CFG, which will clear the maps.
  void exitCFG() {
    InstructionMap.clear();
  }

  /// Add a new instruction to the map.
  void insertInstructionMap(Instruction *Orig, Attr&& At) {
    if (Orig->instrID() > 0)
      InstructionMap[Orig->instrID()] = std::move(At);
  }

  /// Create a copy of this scope.  (Used for lazy rewriting)
  ScopeFrame* clone() { return new ScopeFrame(*this); }

  ScopeFrame() {
    // Variable ID 0 means uninitialized.
    VarMap.push_back(VarMapEntry(nullptr, Attr()));
  }

protected:
  ScopeFrame(const ScopeFrame &F) = default;

  std::vector<VarMapEntry> VarMap;          //< map vars to values
  std::vector<Attr>        InstructionMap;  //< map instrs to values
};



/// ScopeHandlerBase is a base class for implementing traversals that can
/// save, restore, and switch between lexical scopes.
template<class ScopeT>
class ScopeHandlerBase {
public:
  // Return the current context.
  ScopeT* scope() { return ScopePtr; }

  // Switch to a new context, and return the old one.
  ScopeT* switchScope(ScopeT* S) {
    ScopeT* Tmp = ScopePtr;  ScopePtr = S;  return Tmp;
  }

  // Restore an earlier context.
  void restoreScope(ScopeT* OldScope) { ScopePtr = OldScope; }

  ScopeHandlerBase() : ScopePtr(nullptr) { }

  // Takes ownership of S.
  ScopeHandlerBase(ScopeT* S) : ScopePtr(S) { }

  virtual ~ScopeHandlerBase() {
    if (ScopePtr) delete ScopePtr;
  }

protected:
  ScopeT* ScopePtr;
};



/// AttributeGrammar is a base class for attribute-grammar style traversals.
/// It maintains a stack of synthesized attributes that mirror the call stack,
/// and hold traversal results.  During travesal, the reduceX methods should
/// use attr(i) to read the synthesized attributes associated with the i^th
/// subexpression, and they should store result attributes in resultAttr().
template <class Attr, class ScopeT>
class AttributeGrammar : public ScopeHandlerBase<ScopeT> {
public:
  /// Returns the number of synthesized attributes.
  /// When invoked from within reduceX(), this should equal the number of
  /// sub-expressions.
  unsigned numAttrs() const { return Attrs.size() - AttrFrame; }

  /// Get the synthesized attribute for the i^th argument, s.t. i < numAttrs().
  /// Should only be called from reduceX().
  Attr& attr(unsigned i) {
    assert(i < numAttrs() && "Attribute index out of bounds.");
    return Attrs[AttrFrame + i];
  }

  /// Return the synthesized attribute on the top of the stack, which is
  /// the last one produced.
  Attr& lastAttr() {
    assert(numAttrs() > 0 && "No attributes on stack.");
    return Attrs.back();
  }

  /// Get a reference to the synthesized attribute for the result.
  /// The reduceX() methods should write their results to this position.
  Attr& resultAttr() { return Attrs[AttrFrame - 1]; }

  /// Push a new attribute onto the current frame, and return it.
  Attr* pushAttr() {
    Attrs.push_back(Attr());
    return &Attrs.back();
  }

  /// Pop the last attribute off the stack.
  void popAttr() { Attrs.pop_back();  }

  /// Create a new attribute frame, which consists of a new result attribute,
  /// and an empty list of arguments.  Returns an index to the old frame,
  /// which can be used to restore it later.
  unsigned pushAttrFrame() {
    unsigned N = AttrFrame;
    Attrs.push_back(Attr());
    AttrFrame = Attrs.size();
    return N;
  }

  /// Restore the previous attribute frame.  The current frame is discarded,
  /// except for the result attribute, which is pushed onto the argument list
  /// of the previous frame.  N should be the value returned from the prior
  /// pushAttrFrame().
  void restoreAttrFrame(unsigned N) {
    while (Attrs.size() > AttrFrame)
      Attrs.pop_back();
    AttrFrame = N;
  }

  /// Clear all attribute frames.
  void clearAttrFrames() {
    Attrs.clear();
    AttrFrame = 0;
  }

  /// Return true if there are no attributes on the stack.
  bool emptyAttrs() { return Attrs.empty(); }


  void enterScope(VarDecl *Vd)   { /* Override in derived class. */ }
  void exitScope (VarDecl *Vd)   { /* Override in derived class. */ }
  void enterCFG  (SCFG *Cfg)     { this->scope()->enterCFG(Cfg); }
  void exitCFG   (SCFG *Cfg)     { this->scope()->exitSCFG(Cfg); }
  void enterBlock(BasicBlock *B) { /* Override in derived class. */ }
  void exitBlock (BasicBlock *B) { /* Override in derived class. */ }


  AttributeGrammar() : AttrFrame(0)  { }

  // Takes ownership of Sc
  AttributeGrammar(ScopeT* Sc)
      : ScopeHandlerBase<ScopeT>(Sc), AttrFrame(0)
  { }
  ~AttributeGrammar() override { }

protected:
  std::vector<Attr> Attrs;
  unsigned          AttrFrame;
};



/// AGTraversal is a mixin traversal class for use with AttributeGrammar.
/// Self must inherit from AttributeGrammar.
template <class Self, class SuperTv = Traversal<Self>>
class AGTraversal : public SuperTv {
public:
  Self *self() { return static_cast<Self*>(this); }

  // Create a new attribute frame, traverse E within that frame,
  // and return the result on the current attribute stack.
  // The result of the traversal is held in lastAttr().
  template <class T>
  void traverse(T* E, TraversalKind K) {
    // we override traverse to manage contexts and attribute frames.
    unsigned Af = self()->pushAttrFrame();
    auto Cstate = self()->scope()->enterSubExpr(K);

    SuperTv::traverse(E, K);

    if (K == TRV_Instr) {
      Instruction *I = E->asCFGInstruction();
      if (I)
        self()->scope()->insertInstructionMap(I,
          std::move(self()->resultAttr()));
    }

    self()->scope()->exitSubExpr(K, Cstate);
    self()->restoreAttrFrame(Af);
  }


  void traverseWeak(Instruction* E) {
    unsigned Af = self()->pushAttrFrame();
    SuperTv::traverseWeak(E);
    self()->restoreAttrFrame(Af);
  }


  void traverseNull() {
    unsigned Af = self()->pushAttrFrame();
    SuperTv::traverseNull();
    self()->restoreAttrFrame(Af);
  }
};



}  // end namespace til
}  // end namespace ohmu


#endif  // OHMU_TIL_ATTRIBUTEGRAMMAR_H
