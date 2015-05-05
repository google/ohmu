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

namespace ohmu {
namespace til  {


// Basic class for comparison operations.
// CT is the result type for the comparison, e.g. bool for simple equality,
// or int for lexigraphic comparison {-1, 0, 1}.  Must have one value which
// denotes "true".
template <typename Self, typename CT>
class Comparator {
protected:
  Self *self() { return reinterpret_cast<Self *>(this); }

public:
  typedef CT CType;

  /// Compare E1 and E2, which must have the same type.
  CType compareByCase(const SExpr *E1, const SExpr* E2) {
    switch (E1->opcode()) {
#define TIL_OPCODE_DEF(X)                                                     \
    case COP_##X:                                                             \
      return cast<X>(E1)->compare(cast<X>(E2), *self());
#include "TILOps.def"
    }
    return self()->falseResult();
  }
};


///////////////////////////////////////////
// Implement compare for all TIL classes.
///////////////////////////////////////////

template <class C>
typename C::CType VarDecl::compare(const VarDecl* E, C& Cmp) const {
  auto Ct = Cmp.compareIntegers(kind(), E->kind());
  if (Cmp.notTrue(Ct))
    return Ct;
  // Note, we don't compare names, due to alpha-renaming.
  return Cmp.compare(definition(), E->definition());
}

template <class C>
typename C::CType Function::compare(const Function* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compare(VDecl->definition(), E->VDecl->definition());
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.exitScope();
  return Ct;
}

template <class C>
typename C::CType Code::compare(const Code* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(returnType(), E->returnType());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(body(), E->body());
}

template <class C>
typename C::CType Field::compare(const Field* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(range(), E->range());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(body(), E->body());
}

template <class C>
typename C::CType Slot::compare(const Slot* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compareStrings(slotName(), E->slotName());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(definition(), E->definition());
}

template <class C>
typename C::CType Record::compare(const Record* E, C& Cmp) const {
  unsigned N = slots().size();
  unsigned M = E->slots().size();
  typename C::CType Ct = Cmp.compareIntegers(N, M);
  if (Cmp.notTrue(Ct))
    return Ct;
  unsigned Sz = (N < M) ? N : M;
  for (unsigned i = 0; i < Sz; ++i) {
    Ct = Cmp.compare(slots()[i].get(), E->slots()[i].get());
    if (Cmp.notTrue(Ct))
      return Ct;
  }
  return Ct;
}

template <class C>
typename C::CType ScalarType::compare(const ScalarType* E, C& Cmp) const {
  return Cmp.compareIntegers(baseType().asUInt16(), E->baseType().asUInt16());
}


template <class C>
typename C::CType Literal::compare(const Literal* E, C& Cmp) const {
  // TODO: defer actual comparison to LiteralT
  return Cmp.trueResult();
}

template <class C>
typename C::CType Variable::compare(const Variable* E, C& Cmp) const {
  // TODO: compare weak refs.
  return Cmp.comparePointers(variableDecl(), E->variableDecl());
}

template <class C>
typename C::CType Apply::compare(const Apply* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(fun(), E->fun());
  if (Cmp.notTrue(Ct) || (!arg() && !E->arg()))
    return Ct;
  return Cmp.compare(arg(), E->arg());
}

template <class C>
typename C::CType Project::compare(const Project* E, C& Cmp) const {
  typename C::CType Ct;
  if (slotDecl() && E->slotDecl()) {
    Ct = Cmp.comparePointers(slotDecl(), E->slotDecl());
    if (Cmp.notTrue(Ct))
      return Ct;
  }
  else {
    Ct = Cmp.compareStrings(slotName(), E->slotName());
    if (Cmp.notTrue(Ct))
      return Ct;
  }
  if (!record() || !E->record())
    return Cmp.comparePointers(record(), E->record());
  return Cmp.compare(record(), E->record());
}

template <class C>
typename C::CType Call::compare(const Call* E, C& Cmp) const {
  return Cmp.compare(target(), E->target());
}


template <class C>
typename C::CType Alloc::compare(const Alloc* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compareIntegers(allocKind(), E->allocKind());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(initializer(), E->initializer());
}

template <class C>
typename C::CType Load::compare(const Load* E, C& Cmp) const {
  return Cmp.compare(pointer(), E->pointer());
}

template <class C>
typename C::CType Store::compare(const Store* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(destination(), E->destination());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(source(), E->source());
}

template <class C>
typename C::CType ArrayIndex::compare(const ArrayIndex* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(array(), E->array());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(index(), E->index());
}

template <class C>
typename C::CType ArrayAdd::compare(const ArrayAdd* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(array(), E->array());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(index(), E->index());
}

template <class C>
typename C::CType UnaryOp::compare(const UnaryOp* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(unaryOpcode(), E->unaryOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr(), E->expr());
}

template <class C>
typename C::CType BinaryOp::compare(const BinaryOp* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(binaryOpcode(), E->binaryOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  Ct = Cmp.compare(expr0(), E->expr0());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr1(), E->expr1());
}

template <class C>
typename C::CType Cast::compare(const Cast* E, C& Cmp) const {
  typename C::CType Ct =
    Cmp.compareIntegers(castOpcode(), E->castOpcode());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(expr(), E->expr());
}

template <class C>
typename C::CType Phi::compare(const Phi *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Goto::compare(const Goto *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Branch::compare(const Branch *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Return::compare(const Return *E, C &Cmp) const {
  return Cmp.compare(returnValue(), E->returnValue());
}

template <class C>
typename C::CType BasicBlock::compare(const BasicBlock *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType SCFG::compare(const SCFG *E, C &Cmp) const {
  // TODO: implement CFG comparisons
  return Cmp.comparePointers(this, E);
}

template <class C>
typename C::CType Future::compare(const Future* E, C& Cmp) const {
  if (!Result || !E->Result)
    return Cmp.comparePointers(this, E);
  return Cmp.compare(Result, E->Result);
}

template <class C>
typename C::CType Undefined::compare(const Undefined* E, C& Cmp) const {
  return Cmp.trueResult();
}

template <class C>
typename C::CType Wildcard::compare(const Wildcard* E, C& Cmp) const {
  return Cmp.trueResult();
}

template <class C>
typename C::CType Identifier::compare(const Identifier* E, C& Cmp) const {
  return Cmp.compareStrings(idString(), E->idString());
}

template <class C>
typename C::CType Let::compare(const Let* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(variableDecl(), E->variableDecl());
  if (Cmp.notTrue(Ct))
    return Ct;
  Cmp.enterScope(variableDecl(), E->variableDecl());
  Ct = Cmp.compare(body(), E->body());
  Cmp.exitScope();
  return Ct;
}

template <class C>
typename C::CType IfThenElse::compare(const IfThenElse* E, C& Cmp) const {
  typename C::CType Ct = Cmp.compare(condition(), E->condition());
  if (Cmp.notTrue(Ct))
    return Ct;
  Ct = Cmp.compare(thenExpr(), E->thenExpr());
  if (Cmp.notTrue(Ct))
    return Ct;
  return Cmp.compare(elseExpr(), E->elseExpr());
}



class EqualsComparator : public Comparator<EqualsComparator, bool> {
public:
  bool trueResult()     { return true; }
  bool falseResult()    { return false; }
  bool notTrue(bool ct) { return !ct;  }

  bool compareIntegers(unsigned i, unsigned j)       { return i == j; }
  bool compareStrings (StringRef s, StringRef r)     { return s == r; }
  bool comparePointers(const void* P, const void* Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr* E2) {
    if (E1 == E2)
      return true;
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const VarDecl* V1, const VarDecl* V2) { }
  void exitScope() { }

  bool compareVariableRefs(const VarDecl* V1, const VarDecl* V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    EqualsComparator Eq;
    return Eq.compare(E1, E2);
  }
};



class MatchComparator : public Comparator<MatchComparator, bool> {
public:
  bool trueResult()     { return true;  }
  bool falseResult()    { return false; }
  bool notTrue(bool ct) { return !ct;   }

  bool compareIntegers(unsigned i, unsigned j)       { return i == j; }
  bool compareStrings (StringRef s, StringRef r)     { return s == r; }
  bool comparePointers(const void* P, const void* Q) { return P == Q; }

  bool compare(const SExpr *E1, const SExpr* E2) {
    if (E1 == E2)
      return true;
    // Wildcards match anything.
    if (E1->opcode() == COP_Wildcard || E2->opcode() == COP_Wildcard)
      return true;
    // otherwise normal equality.
    if (E1->opcode() != E2->opcode())
      return false;
    return compareByCase(E1, E2);
  }

  // TODO -- handle alpha-renaming of variables
  void enterScope(const VarDecl* V1, const VarDecl* V2) { }
  void exitScope() { }

  bool compareVariableRefs(const VarDecl* V1, const VarDecl* V2) {
    return V1 == V2;
  }

  static bool compareExprs(const SExpr *E1, const SExpr* E2) {
    MatchComparator Matcher;
    return Matcher.compare(E1, E2);
  }
};


} // end namespace til
} // end namespace ohmu

#endif  // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYCOMPARE_H
