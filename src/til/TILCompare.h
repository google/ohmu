//===- TILCompare.h --------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a framework for comparing SExprs.  A comparison is an
// operation which involves traversing two SExprs; examples are equality,
// matching, subtyping, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILCOMPARE_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_TILCOMPARE_H

#include "TIL.h"
#include "AnnotationImpl.h"

namespace ohmu {
namespace til  {

// Basic class for comparison operations.
// CT is the result type for the comparison, e.g. bool for simple equality,
// or int for lexigraphic comparison {-1, 0, 1}.  Must have at least one value
// which denotes "true" and one which denotes "false".
template <class Self, class CT>
class Comparator {
public:
  Self *self() { return reinterpret_cast<Self *>(this); }

  typedef CT CType;

public:
  /// Shortcuts comparison if an earlier comparison already reached a false
  /// result.
  void compare(const SExpr *E1, const SExpr *E2) {
    if (!self()->success())
      return;
    self()->compareSExpr(E1, E2);
  }

  /// Default comparison of SExpr calls compareByCase if the expressions are of
  /// equal kind.
  void compareSExpr(const SExpr *E1, const SExpr *E2) {
    if (E1 == E2)
       return;
    if (!E1 || !E2) {
      this->self()->fail();
      return;
    }
    if (E1->opcode() == E2->opcode())
      compareByCase(E1, E2);
    else
      self()->fail();
  }

  /// Compare E1 and E2, which must have the same type.
  void compareByCase(const SExpr *E1, const SExpr *E2) {
    switch (E1->opcode()) {
#define TIL_OPCODE_DEF(X)                                                     \
    case COP_##X:                                                             \
      self()->compare##X(cast<X>(E1), cast<X>(E2)); break;
#include "TILOps.def"
    }
    self()->compareAllAnnotations(E1->annotations(), E2->annotations());
  }

#define TIL_OPCODE_DEF(X)                                                     \
  void compare##X(const X *E1, const X *E2);
#include "TILOps.def"

  /// Compare two (sorted) lists of annotations.
  void compareAllAnnotations(const Annotation *A1, const Annotation *A2) {
    while (A1 || A2) {
      if (A1 == A2)
        return;

      // Allow for actively ignoring annotations:
      if (A1 && ignoreAnnotationByCase(A1)) {
        A1 = A1->next();
        continue;
      }
      if (A2 && ignoreAnnotationByCase(A2)) {
        A2 = A2->next();
        continue;
      }

      // One of the annotation lists is shorter than the other:
      if (!A1) {
        self()->compareMissingAnnotation(nullptr, A2);
        A2 = A2->next();
        continue;
      }
      if (!A2) {
        self()->compareMissingAnnotation(A1, nullptr);
        A1 = A1->next();
        continue;
      }

      // Sorted lists do not start with the same annotation kind, one must be
      // missing:
      if (A1->kind() != A2->kind()) {
        if (A1->kind() < A2->kind()) {
          self()->compareMissingAnnotation(A1, nullptr);
          A1 = A1->next();
        } else {
          self()->compareMissingAnnotation(nullptr, A2);
          A2 = A2->next();
        }
        continue;
      }

      // Both lists start with the same kind of annotation, compare that
      // annotation by kind.
      self()->compareByCase(A1, A2);
      A1 = A1->next();
      A2 = A2->next();
    }
  }

  /// One of the two arguments is a nullptr, indicating that on that side an
  /// annotation of the corresponding kind is missing.
  void compareMissingAnnotation(const Annotation *A1, const Annotation *A2) {
    self()->fail();
  }

  /// Dispatches the first annotation in the list by kind. Assumes both lists
  /// start with annotations that are of the same kind.
  void compareByCase(const Annotation *A1, const Annotation *A2) {
    switch (A1->kind()) {
#define TIL_ANNKIND_DEF(X)                                                     \
    case ANNKIND_##X:                                                          \
      cast<X>(A1)->compare(cast<X>(A2), self()); break;
#include "TILAnnKinds.def"
    }
  }

  bool ignoreAnnotationByCase(const Annotation *A) {
    switch (A->kind()) {
#define TIL_ANNKIND_DEF(X)                                                     \
    case ANNKIND_##X:                                                          \
      return self()->ignoreAnnotation(cast<X>(A));
#include "TILAnnKinds.def"
    }
  }

  template <class Ann>
  bool ignoreAnnotation(const Ann *A) {
    return false;
  }

public:
  /// Compare two sequences of SExprRef's, assumes both are of the same length.
  template <class Iter>
  void compareRefSequence(Iter it1, Iter it2, Iter end1) {
    while (it1 != end1) {
      self()->compare((*it1).get(), (*it2).get());
      ++it1;
      ++it2;
    }
  }

  /// Compare two sequences of SExpr*'s, assumes both are of the same length.
  template <class Iter>
  void compareSequence(Iter it1, Iter it2, Iter end1) {
    while (it1 != end1) {
      self()->compare((*it1), (*it2));
      ++it1;
      ++it2;
    }
  }

public:
  // Relate blocks with possibly different IDs.
  std::vector<size_t> BlockMap;

  void blockMapCompare(size_t ID1, size_t ID2) {
    assert (BlockMap.size() > ID1 && "Looking up unknown block ID.");
    self()->compareScalarValues(BlockMap.at(ID1), ID2);
  }
  void blockMapClear() { BlockMap.clear(); }
  void blockMapResize(size_t S) { BlockMap.resize(S); }
  void blockMapStore(size_t ID1, size_t ID2) { BlockMap[ID1] = ID2; }

};

///////////////////////////////////////////
// Implement compare for all TIL classes.
///////////////////////////////////////////

template <class S, class C>
void Comparator<S,C>::compareVarDecl(const VarDecl *E1, const VarDecl *E2) {
  self()->compareScalarValues(E1->kind(), E2->kind());
  self()->compare(E1->definition(), E2->definition());
}

template <class S, class C>
void Comparator<S,C>::compareFunction(const Function *E1, const Function *E2) {
  self()->compareScalarValues(E1->isSelfApplicable(), E2->isSelfApplicable());
  if (!E1->isSelfApplicable())
    self()->compare(E1->variableDecl()->definition(),
        E2->variableDecl()->definition());
  self()->enterScope(E1->variableDecl(), E2->variableDecl());
  self()->compare(E1->body(), E2->body());
  self()->exitScope(E1->variableDecl());
}

template <class S, class C>
void Comparator<S,C>::compareCode(const Code *E1, const Code* E2) {
  self()->compare(E1->returnType(), E2->returnType());
  self()->compare(E1->body(), E2->body());
}

template <class S, class C>
void Comparator<S,C>::compareField(const Field *E1, const Field* E2) {
  self()->compare(E1->range(), E2->range());
  self()->compare(E1->body(), E2->body());
}

template <class S, class C>
void Comparator<S,C>::compareSlot(const Slot *E1, const Slot *E2) {
  self()->compareScalarValues(E1->slotName(), E2->slotName());
  self()->compare(E1->definition(), E2->definition());
}

template <class S, class C>
void Comparator<S,C>::compareRecord(const Record *E1, const Record *E2) {
  unsigned N = E1->slots().size();
  unsigned M = E2->slots().size();
  self()->compareScalarValues(N, M);
  unsigned Sz = (N < M) ? N : M;
  for (unsigned i = 0; i < Sz; ++i) {
    self()->compare(E1->slots()[i].get(), E2->slots()[i].get());
  }
  self()->compare(E1->parent(), E2->parent());
}

template <class S, class C>
void Comparator<S,C>::compareScalarType(const ScalarType *E1,
    const ScalarType *E2) {
  self()->compareScalarValues(E1->baseType().asUInt16(), E2->baseType().asUInt16());
}


template<class Cmp>
class LitComparator {
public:
  template<class Ty>
  class Actor {
  public:
    typedef void ReturnType;
    static void defaultAction(const Literal *E1, const Literal *E2, Cmp *C) {
    }
    static void action(const Literal *E1, const Literal *E2, Cmp *C) {
      C->self()->compareScalarValues(E1->as<Ty>()->value(),
          E2->as<Ty>()->value());
    }
  };
};

template <class S, class C>
void Comparator<S,C>::compareLiteral(const Literal *E1, const Literal *E2) {
  self()->compareBaseTypes(E1->baseType(), E2->baseType());
  if (E1->baseType() == E2->baseType())
    BtBr< LitComparator<Comparator<S,C>>::template Actor >::branch(
        E1->baseType(), E1, E2, this);
}

template <class S, class C>
void Comparator<S,C>::compareVariable(const Variable *E1, const Variable *E2) {
  self()->compareWeakRefs(E1->variableDecl(), E2->variableDecl());
}

template <class S, class C>
void Comparator<S,C>::compareApply(const Apply *E1, const Apply *E2) {
  self()->compare(E1->fun(), E2->fun());
  if (E1->arg() || !E2->arg())
    self()->compare(E1->arg(), E2->arg());
}

template <class S, class C>
void Comparator<S,C>::compareProject(const Project *E1, const Project *E2) {
  if (E1->slotDecl() && E2->slotDecl())
    self()->compareScalarValues(E1->slotDecl(), E2->slotDecl());
  else
    self()->compareScalarValues(E1->slotName(), E2->slotName());

  if (!E1->record() || !E2->record())
    self()->compareScalarValues(E1->record(), E2->record());
  else
    self()->compare(E1->record(), E2->record());
}

template <class S, class C>
void Comparator<S,C>::compareCall(const Call *E1, const Call* E2) {
  self()->compare(E1->target(), E2->target());
}

template <class S, class C>
void Comparator<S,C>::compareAlloc(const Alloc *E1, const Alloc *E2) {
  self()->compareScalarValues(E1->allocKind(), E2->allocKind());
  self()->compare(E1->initializer(), E2->initializer());
}

template <class S, class C>
void Comparator<S,C>::compareLoad(const Load *E1, const Load *E2) {
  self()->compare(E1->pointer(), E2->pointer());
}

template <class S, class C>
void Comparator<S,C>::compareStore(const Store *E1, const Store *E2) {
  self()->compare(E1->destination(), E2->destination());
  self()->compare(E1->source(), E2->source());
}

template <class S, class C>
void Comparator<S,C>::compareArrayIndex(const ArrayIndex *E1,
    const ArrayIndex *E2) {
  self()->compare(E1->array(), E2->array());
  self()->compare(E1->index(), E2->index());
}

template <class S, class C>
void Comparator<S,C>::compareArrayAdd(const ArrayAdd *E1, const ArrayAdd *E2) {
  self()->compare(E1->array(), E2->array());
  self()->compare(E1->index(), E2->index());
}

template <class S, class C>
void Comparator<S,C>::compareUnaryOp(const UnaryOp *E1, const UnaryOp *E2) {
  self()->compareScalarValues(E1->unaryOpcode(), E2->unaryOpcode());
  self()->compare(E1->expr(), E2->expr());
}

template <class S, class C>
void Comparator<S,C>::compareBinaryOp(const BinaryOp *E1, const BinaryOp *E2) {
  self()->compareScalarValues(E1->binaryOpcode(), E2->binaryOpcode());
  self()->compare(E1->expr0(), E2->expr0());
  self()->compare(E1->expr1(), E2->expr1());
}

template <class S, class C>
void Comparator<S,C>::compareCast(const Cast *E1, const Cast* E2) {
  self()->compareScalarValues(E1->castOpcode(), E2->castOpcode());
  self()->compare(E1->expr(), E2->expr());
}

template <class S, class C>
void Comparator<S,C>::comparePhi(const Phi *E1, const Phi *E2) {
  self()->compareScalarValues(E1->values().size(), E2->values().size());
  self()->compareScalarValues(E1->status(), E2->status());
}

template <class S, class C>
void Comparator<S,C>::compareGoto(const Goto *E1, const Goto *E2) {
  self()->blockMapCompare(E1->targetBlock()->blockID(),
      E2->targetBlock()->blockID());
  self()->compareScalarValues(E1->phiIndex(), E2->phiIndex());

  auto &A1 = E1->targetBlock()->arguments();
  auto &A2 = E2->targetBlock()->arguments();

  if (A1.size() != A2.size()) {
    self()->fail();
  } else {
    auto Iter1 = A1.begin();
    auto Iter2 = A2.begin();
    while (Iter1 != A1.end()) {
      self()->compare((*Iter1)->values().at(E1->phiIndex()).get(),
          (*Iter2)->values().at(E2->phiIndex()).get());
      ++Iter1;
      ++Iter2;
    }
  }
}

template <class S, class C>
void Comparator<S,C>::compareBranch(const Branch *E1, const Branch *E2) {
  self()->compare(E1->condition(), E2->condition());
  self()->blockMapCompare(E1->thenBlock()->blockID(),
      E2->thenBlock()->blockID());
  self()->blockMapCompare(E1->elseBlock()->blockID(),
      E2->elseBlock()->blockID());
}

template <class S, class C>
void Comparator<S,C>::compareReturn(const Return *E1, const Return *E2) {
  self()->compare(E1->returnValue(), E2->returnValue());
}

template <class S, class C>
void Comparator<S,C>::compareBasicBlock(const BasicBlock *E1,
    const BasicBlock *E2) {
  self()->compareScalarValues(E1->numArguments(), E2->numArguments());
  self()->compareScalarValues(E1->numInstructions(), E2->numInstructions());
  self()->compareScalarValues(E1->numPredecessors(), E2->numPredecessors());
  self()->compareScalarValues(E1->numSuccessors(), E2->numSuccessors());

  if (E1->arguments().size() != E2->arguments().size())
    self()->fail();
  else
    self()->compareSequence(E1->arguments().begin(), E2->arguments().begin(),
        E1->arguments().end());

  if (E1->instructions().size() != E2->instructions().size())
    self()->fail();
  else
    self()->compareSequence(E1->instructions().begin(),
        E2->instructions().begin(), E1->instructions().end());
  self()->compare(E1->terminator(), E2->terminator());
}

template <class S, class C>
void Comparator<S,C>::compareSCFG(const SCFG *E1, const SCFG *E2) {
  self()->compareScalarValues(E1->numBlocks(), E2->numBlocks());
  self()->compareScalarValues(E1->numInstructions(), E2->numInstructions());

  if (E1->numBlocks() != E2->numBlocks()) {
    self()->fail();
  } else {
    // Store mapping between block ids. For comparison we assume that the CFGs
    // are in normalized form, hence the blocks should be in the same order.
    auto Iter1 = E1->blocks().begin();
    auto Iter2 = E2->blocks().begin();
    self()->blockMapResize(E1->numBlocks());
    while (Iter1 != E1->blocks().end()) {
      self()->blockMapStore((*Iter1).get()->blockID(),
          (*Iter2).get()->blockID());
      ++Iter1;
      ++Iter2;
    }
    self()->compareRefSequence(E1->blocks().begin(), E2->blocks().begin(),
        E1->blocks().end());
    self()->blockMapClear();
  }
}

template <class S, class C>
void Comparator<S,C>::compareFuture(const Future *E1, const Future *E2) {
  if (!E1->getResult() || !E2->getResult())
    self()->compareScalarValues(E1, E2);
  else
    self()->compare(E1->getResult(), E2->getResult());
}

template <class S, class C>
void Comparator<S,C>::compareUndefined(const Undefined *E1,
    const Undefined *E) {
}

template <class S, class C>
void Comparator<S,C>::compareWildcard(const Wildcard *E1, const Wildcard *E) {
}

template <class S, class C>
void Comparator<S,C>::compareIdentifier(const Identifier *E1,
    const Identifier *E2) {
  self()->compareScalarValues(E1->idString(), E2->idString());
}

template <class S, class C>
void Comparator<S,C>::compareLet(const Let *E1, const Let *E2) {
  self()->enterScope(E1->variableDecl(), E2->variableDecl());
  self()->compare(E1->body(), E2->body());
  self()->exitScope(E1->variableDecl());
}

template <class S, class C>
void Comparator<S,C>::compareIfThenElse(const IfThenElse *E1,
    const IfThenElse *E2) {
  self()->compare(E1->condition(), E2->condition());
  self()->compare(E1->thenExpr(), E2->thenExpr());
  self()->compare(E1->elseExpr(), E2->elseExpr());
}


/// DefaultComparator implements empty versions of the lexical scope
/// enter/exit routines for traversals.
template <class Self, class CType>
class DefaultComparator : public Comparator<Self, CType> {
public:
  void enterScope(const VarDecl* V1, const VarDecl* V2) { }
  void exitScope(const VarDecl* V1) { }

  CType compareWeakRefs(const VarDecl* V1, const VarDecl* V2) {
    return this->self()->compareScalarValues(V1, V2);
  }
};


/// AlphaLetComparator ignores differences up to alpha-renaming and let
/// bindings.
template <class Self, class CType>
class AlphaLetComparator : public Comparator<Self, CType> {
public:
  void enterScope(const VarDecl *V1, const VarDecl *V2) {
    // Function-bindings can be related directly.
    if (V1->kind() == VarDecl::VK_Fun || V1->kind() == VarDecl::VK_SFun)
      cache_store(V1->varIndex(), V2->varIndex());
    // Other kinds are ignored and resolved when weak references are compared.
  }

  void exitScope(const VarDecl *V1) { cache_clear(V1->varIndex()); }

  /// If we already mapped V1 to some index i, compare i to V2's index.
  /// Otherwise, compare the definitions of V1 and V2, and cache their mapping
  /// for future comparisons.
  void compareWeakRefs(const VarDecl *V1, const VarDecl *V2) {
    unsigned i = cache_lookup(V1->varIndex());
    if (i != 0) {
      this->self()->compareScalarValues(i, V2->varIndex());
    } else {
      this->self()->compare(V1->definition(), V2->definition());
      cache_store(V1->varIndex(), V2->varIndex());
    }
  }

  void compareSExpr(const SExpr *E1, const SExpr *E2) {
    if (E1 == E2)
      return;
    if (!E1 || !E2) {
      this->self()->fail();
      return;
    }
    if (E1->opcode() != E2->opcode()) {
      // If opcodes don't match, try to skip let-definitions or look up a
      // variable that is bounded by a let-definition.
      if (E1->opcode() == COP_Let)
        this->self()->compare(cast<Let>(E1)->body(), E2);
      else if (E1->opcode() == COP_Variable &&
          cast<Variable>(E1)->variableDecl()->kind() == VarDecl::VK_Let)
        this->self()->compare(
            cast<Variable>(E1)->variableDecl()->definition(), E2);

      else if (E2->opcode() == COP_Let)
        this->self()->compare(E1, cast<Let>(E2)->body());
      else if (E2->opcode() == COP_Variable &&
          cast<Variable>(E2)->variableDecl()->kind() == VarDecl::VK_Let)
        this->self()->compare(E1,
            cast<Variable>(E2)->variableDecl()->definition());

      else
        this->self()->compareOpcodes(E1->opcode(), E2->opcode());
    } else {
      this->self()->compareByCase(E1, E2);
    }
  }

private:
  std::vector<unsigned> Cache;

  unsigned cache_lookup(unsigned i) {
    if (Cache.size() <= i)
      return 0;
    return Cache[i];
  }

  void cache_store(unsigned i, unsigned j) {
    Cache.resize(i+1, 0);
    Cache[i] = j;
  }

  // Can't use pop_back since variable i was only added if it was actually
  // referenced.
  void cache_clear(unsigned i) {
    Cache.resize(i);
  }
};


class EqualsComparator : public AlphaLetComparator<EqualsComparator, bool> {
public:
  void compareBaseTypes(BaseType b, BaseType c)       { if (b != c) fail(); }

  template <class Lit>
  void compareScalarValues (Lit i, Lit j)             { if (i != j) fail(); }

  void compareOpcodes  (TIL_Opcode O, TIL_Opcode P)   { if (O != P) fail(); }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    EqualsComparator Eq;
    Eq.compare(E1, E2);
    return Eq.SuccessState;
  }

public:
  bool SuccessState;

  void fail() { SuccessState = false; }

  bool success() { return SuccessState; }

  EqualsComparator() : SuccessState(true) { }

};


class MatchComparator : public AlphaLetComparator<EqualsComparator, bool> {
public:
  void compareBaseTypes(BaseType b, BaseType c)       { if (b != c) fail(); }

  template <class Lit>
  void compareScalarValues (Lit i, Lit j)             { if (i != j) fail(); }

  void compareOpcodes  (TIL_Opcode O, TIL_Opcode P) {
   if (O != P && O != COP_Wildcard && P != COP_Wildcard)
     fail();
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    MatchComparator Eq;
    Eq.compare(E1, E2);
    return Eq.SuccessState;
  }

public:
  bool SuccessState;

  void fail() { SuccessState = false; }

  bool success() { return SuccessState; }

  MatchComparator() : SuccessState(true) { }
};

}  // end namespace til
}  // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYCOMPARE_H
