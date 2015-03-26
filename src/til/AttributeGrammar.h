//===- AttributeGrammar.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
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



/// A Substitution stores a list of terms that will be substituted for free
/// variables.  Each variable has a deBruin index, which is used to look up the
/// substitution for that variable.  A common pattern that occurs when doing
/// type checking or inlining of functions within a nested context, is for
/// the first 'n' variables to be substituted for themselves; this is a null
/// substitution.
template<class Attr>
class Substitution {
public:
  unsigned nullVars()    const { return NullVars; }
  unsigned numVarAttrs() const { return VarAttrs.size(); }
  unsigned size()        const { return NullVars + VarAttrs.size(); }
  bool     empty()       const { return size() == 0; }

  Attr& var(unsigned i) {
    assert(i >= NullVars && i < size() && "Index out of bounds.");
    return VarAttrs[i-NullVars];
  }

  std::vector<Attr>& varAttrs() { return VarAttrs; }

  bool isNull(unsigned Idx) { return Idx < NullVars; }

  void push_back(const Attr& At) {
    assert(NullVars > 0);   // Index 0 is reserved.
    VarAttrs.push_back(At);
  }
  void push_back(Attr&& At) {
    assert(NullVars > 0);   // Index 0 is reserved.
    VarAttrs.push_back(std::move(At));
  }

  void clear() {
    NullVars = 0;
    VarAttrs.clear();
  }

  void init(unsigned Nv) {
    assert(empty() && "Already initialized.");
    NullVars = Nv;
  }

  /// Create a substitution from the variable mapping in S.
  template<class ScopeT>
  void initFromScope(ScopeT* S) {
    assert(empty() && "Already initialized.");
    NullVars = S->nullVars();
    unsigned n = S->numVars();
    for (unsigned i = NullVars; i < n; ++i) {
      VarAttrs.push_back(S->var(i));
    }
  }

  Substitution() : NullVars(0) { }
  Substitution(unsigned Nv) : NullVars(Nv) { }
  Substitution(const Substitution& S) = default;
  Substitution(Substitution&& S)      = default;

  Substitution<Attr>& operator=(const Substitution<Attr> &S) = default;
  Substitution<Attr>& operator=(Substitution<Attr> &&S)      = default;

private:
  unsigned          NullVars;   ///< Number of null variables
  std::vector<Attr> VarAttrs;   ///< Synthesized attributes for remaining vars.
};



/// A ScopeFrame maps variables in the lexical context to synthesized
/// attributes.  In particular, it tracks attributes for:
/// (1) Ordinary variables (i.e. function parameters).
/// (2) Instructions (which are essentially let-variables.)
///
/// Scopes are often used in conjunction with substitutions, so the same
/// concepts of null mapping apply here.
///
template<class Attr, typename ExprStateT=int>
class ScopeFrame {
public:
  struct VarMapEntry {
    VarMapEntry() : VDecl(nullptr) { }
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

  /// Return the number of variables with a null mapping.
  /// Variables with deBruin index < nullVars() have no synthesized attribute.
  unsigned nullVars() const { return NullVars; }

  /// Return the number of variables in the context.
  unsigned numVars() const { return NullVars + VarMap.size(); }

  /// Return true if the variable with index Idx has a null mapping.
  bool isNull(unsigned Idx) { return Idx < NullVars; }

  /// Return the attribute for the i^th variable (VarDecl) in the scope
  /// Note that var(0) is reserved; 0 denotes an undefined variable.
  Attr& var(unsigned i) {
    assert(i >= NullVars && i < numVars() && "Index out of bounds.");
    return VarMap[i-NullVars].VarAttr;
  }

  /// Return the attribute for the variable declared by Orig.
  Attr& lookupVar(VarDecl *Orig) { return var(Orig->varIndex()); }

  /// Return the VarDecl -> Attr map entry for the i^th variable.
  VarMapEntry& entry(unsigned i) {
    assert(i >= NullVars && i < numVars() && "Index out of bounds.");
    return VarMap[i-NullVars];
  }

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

  /// Create a new scope where the first Nv variables have a null substitution.
  ScopeFrame(unsigned Nv) : NullVars(Nv) { }

  /// Create a new scope from a substitution.
  ScopeFrame(Substitution<Attr>&& Subst) : NullVars(Subst.nullVars()) {
    assert(Subst.nullVars() > 0 && "Substitution cannot be empty.");
    for (unsigned i=NullVars,n=Subst.size(); i < n; ++i) {
      VarMap.emplace_back(nullptr, std::move(Subst.var(i)));
    }
    Subst.clear();
  }

  /// Default constructor.
  ScopeFrame() : NullVars(1) { }  // deBruin index 0 is reserved

protected:
  ScopeFrame(const ScopeFrame &F) = default;

  unsigned                 NullVars;        ///< vars with no attributes.
  std::vector<VarMapEntry> VarMap;          ///< map vars to values
  std::vector<Attr>        InstructionMap;  ///< map instrs to values
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
  {
    // TODO: FIXME!  This is to prevent memory corruption.
    Attrs.reserve(100000);
  }
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
