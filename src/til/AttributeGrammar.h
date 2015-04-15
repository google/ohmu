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


/// Synthesized attributes use for term rewriting.
class CopyAttr {
public:
  CopyAttr() : Exp(nullptr) { }
  explicit CopyAttr(SExpr* E) : Exp(E) { }

  CopyAttr(const CopyAttr &A) = default;
  CopyAttr(CopyAttr &&A)      = default;

  CopyAttr& operator=(const CopyAttr &A) = default;
  CopyAttr& operator=(CopyAttr &&A)      = default;

  SExpr* Exp;    ///< This is the residual, or rewritten term.
};



/// A Substitution stores a list of terms that will be substituted for free
/// variables.  Each variable has a deBruin index, which is used to look up the
/// substitution for that variable.  Note that a "substitution" involves not
/// just the term, but all synthesized attributes for the term.
///
/// A common pattern that occurs when doing type checking or inlining of
/// functions within a nested context, is for the first 'n' variables to be
/// substituted for themselves.  A substitution of a variable for itself is a
/// null substitution; and we provide special handling which optimizes for that
/// case.
template<class Attr>
class Substitution {
public:
  /// Return number of initial "null" substitutions.
  unsigned numNullVars() const { return NullVars; }

  /// Return number of substitutions (after the initial 'n' null)
  unsigned numSubstVars() const { return VarAttrs.size(); }

  /// Return total number of variables (null and substituted)
  unsigned size() const { return NullVars + VarAttrs.size(); }

  /// Return true if this is an empty substitution
  bool empty() const { return size() == 0; }

  /// Return true if the i^th variable has a null substitution.
  bool isNull(unsigned i) { return i < NullVars; }

  /// Return the substitution for the i^th variable, which cannot be null.
  Attr& var(unsigned i) {
    assert(i >= NullVars && i < size() && "Index out of bounds.");
    return VarAttrs[i-NullVars];
  }

  /// Return the list of non-null substitutions.
  std::vector<Attr>& varAttrs() { return VarAttrs; }

  /// Push n null substitutions.  This Substitution must be entirely null.
  void push_back_null(unsigned n) {
    assert(VarAttrs.size() == 0);
    NullVars += n;
  }

  /// Push a new substitution onto the end.
  void push_back(const Attr& At) {
    assert(NullVars > 0);   // Index 0 is reserved.
    VarAttrs.push_back(At);
  }

  /// Push a new substitution onto the end.
  void push_back(Attr&& At) {
    assert(NullVars > 0);   // Index 0 is reserved.
    VarAttrs.push_back(std::move(At));
  }

  /// Pop the last substitution off of the end.
  void pop_back() {
    if (VarAttrs.size() > 0) {
      VarAttrs.pop_back();
      return;
    }
    assert(NullVars > 0 && "Empty Substitution");
    --NullVars;
  }

  /// Clear all substitutions, including null ones.
  void clear() {
    NullVars = 0;
    VarAttrs.clear();
  }

  /// Initialize first Nv variables to null.
  void init(unsigned Nv) {
    assert(empty() && "Already initialized.");
    NullVars = Nv;
  }

  Substitution() : NullVars(0) { }
  Substitution(unsigned Nv) : NullVars(Nv) { }
  Substitution(const Substitution& S) = default;
  Substitution(Substitution&& S)      = default;

  Substitution<Attr>& operator=(const Substitution<Attr> &S) = default;
  Substitution<Attr>& operator=(Substitution<Attr> &&S)      = default;

protected:
  unsigned          NullVars;   ///< Number of null variables
  std::vector<Attr> VarAttrs;   ///< Synthesized attributes for remaining vars.
};



/// A ScopeFrame is a Substitution with additional information to more
/// fully track lexical scope.  In particular:
///  - It also tracks variable declarations in the current scope
///  - It stores substitutions for instruction IDs in a CFG.
template<class Attr, typename LocStateT=bool>
class ScopeFrame  {
public:
  const Substitution<Attr>& substitution() { return Subst; }

  unsigned numNullVars()  const { return Subst.numNullVars(); }
  unsigned numSubstVars() const { return Subst.numSubstVars(); }
  unsigned size()         const { return Subst.size(); }
  bool     empty()        const { return Subst.empty(); }
  bool     isNull(unsigned i)   { return Subst.isNull(i); }
  Attr&    var(unsigned i)      { return Subst.var(i); }


  /// Lightweight state that is saved and restored in each subexpression.
  typedef LocStateT LocationState;

  /// Change state to enter a sub-expression.
  /// Subclasses should override it.
  LocationState enterSubExpr(TraversalKind K) { return LocationState(); }

  /// Restore state when exiting a sub-expression.
  /// Subclasses should override it.
  void exitSubExpr(TraversalKind K, LocationState S) { }

  /// Enter a function scope (or apply a function), by mapping Orig -> At.
  void enterScope(VarDecl *Orig, Attr&& At) {
    // Assign indices to variables if they haven't been assigned yet.
    if (Orig) {
      // TODO: FIXME!  Orig should always be specified.
      if (Orig->varIndex() == 0)
        Orig->setVarIndex(size());
      else
        assert(Orig->varIndex() == size() && "De Bruijn index mismatch.");
    }

    Subst.push_back( std::move(At) );
    VarDeclMap.push_back(Orig);
  }

  /// Enter scope of n null substitutions.
  void enterNullScope(unsigned n) {
    Subst.push_back_null(n);
    for (unsigned i = 0; i < n; ++i)
      VarDeclMap.push_back(nullptr);
  }

  void exitScope() {
    Subst.pop_back();
    VarDeclMap.pop_back();
  }

  void enterCFG(SCFG *Orig) {
    assert(InstructionMap.size() == 0 && "No support for nested CFGs");
    InstructionMap.resize(Orig->numInstructions());
  }

  void exitCFG() {
    InstructionMap.clear();
  }

  void enterBlock(BasicBlock* B) { }
  void exitBlock() { }

  /// Return the declaration for the i^th variable.
  VarDecl* varDecl(unsigned i) { return VarDeclMap[i]; }

  /// Return the substitution for the i^th instruction.
  Attr& instr(unsigned i) { return InstructionMap[i]; }

  /// Add a new instruction to the map.
  void insertInstructionMap(Instruction *Orig, Attr&& At) {
    assert(Orig->instrID() > 0 && "Invalid instruction.");
    InstructionMap[Orig->instrID()] = std::move(At);
  }

  /// Create a copy of this scope.  (Used for lazy rewriting)
  ScopeFrame* clone() { return new ScopeFrame(*this); }

  /// Default constructor.
  ScopeFrame() : Subst(1) {
    VarDeclMap.push_back(nullptr);  // deBruin index 0 is reserved
  }

  /// Create a new scope from a substitution.
  ScopeFrame(Substitution<Attr>&& S) : Subst(std::move(S)) {
    for (unsigned i = 0, n = Subst.size(); i < n; ++i)
      VarDeclMap.push_back(nullptr);
  }

  virtual ~ScopeFrame() { }

protected:
  ScopeFrame(const ScopeFrame &F) = default;
  ScopeFrame(ScopeFrame &&F)      = default;

  Substitution<Attr>     Subst;
  std::vector<VarDecl*>  VarDeclMap;      ///< map indices to VarDecls
  std::vector<Attr>      InstructionMap;  ///< map instrs to attributes
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

  /*** ScopeHandler Routines ***/

  typedef typename ScopeT::LocationState LocationState;
  LocationState enterSubExpr(TraversalKind K) {
    return this->scope()->enterSubExpr(K);
  }
  void exitSubExpr(TraversalKind K, LocationState S) {
    this->scope()->exitSubExpr(K, S);
  }

  void enterScope(VarDecl *Vd)   { this->scope()->enterScope(Vd); }
  void exitScope (VarDecl *Vd)   { this->scope()->exitScope();    }
  void enterCFG  (SCFG *Cfg)     { this->scope()->enterCFG(Cfg);  }
  void exitCFG   (SCFG *Cfg)     { this->scope()->exitSCFG();     }
  void enterBlock(BasicBlock *B) { this->scope()->enterBlock(B);  }
  void exitBlock (BasicBlock *B) { this->scope()->exitBlock();    }

  /*** Constructor and destructor ***/

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


  AttributeGrammar() : AttrFrame(0)  { }

  // Takes ownership of Sc
  AttributeGrammar(ScopeT* Sc)
      : ScopeHandlerBase<ScopeT>(Sc), AttrFrame(0) {
    // TODO: FIXME!  Resizing may cause memory corruption.
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
    SuperTv::traverse(E, K);
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
