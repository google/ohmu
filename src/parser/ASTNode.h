//===- ASTNode.h -----------------------------------------------*- C++ --*-===//
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
// This file defines a very simple language that the parser uses for for
// building ASTs.  It mimics the Lisp S-Expression syntax, but is even simpler.
//
// * Terminals are either variables, which refer to results on the parse
//   stack, or strings, which refer to a token in the input file.
// * Non-terminals are commands which construct expressions in the target
//   language.
// * The AST format also has rudimentary support for lists.
//   A list is either [], the empty list, or (append list item)
//
// Syntax:
//   x                -- variable  (defined in the grammar file)
//   "foo"            -- string literal
//   []               -- empty list
//   (append as a)    -- append element a to list as
//   (f arg1.. argn)  -- construct ast node named f in target language.
//
// Examples:  (assuming ohmu is the target language.)
//   (integer "1234")                            // create integer literal
//   (apply (identifier "foo") (identifier "y")) // create expr  'foo(y)'
//   (record (append [] (slot "bar" (...))))     // create record { bar: ... }
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_AST_NODE_H
#define OHMU_AST_NODE_H

#include <iostream>

#include "base/Util.h"

namespace ohmu {
namespace parsing {
namespace ast {


class ASTNode {
public:
  enum Opcode {
    AST_None = 0,
    AST_Variable,    // Variable (variables in parse rule actions).
    AST_TokenStr,    // Token in source file.  (String literal.)
    AST_Construct,   // Constructor for an expression in the target language.
    AST_EmptyList,   // Empty sequence of expressions.
    AST_Append,      // Append to sequence.
  };

  Opcode opcode() const {
    return static_cast<Opcode>(opcode_);
  }

  // The arity of this expression.  (Should be 0 except for Construct)
  unsigned char arity() const { return arity_; }

  // The target language opcode for this expression.  (0 except for Construct)
  unsigned short langOpcode() const { return langop_; }

  // Set the target language opcode.
  void setLangOpcode(unsigned short lop) { langop_ = lop; }

  virtual ~ASTNode() { }

protected:
  ASTNode() = delete;
  ASTNode(unsigned char op)
      : opcode_(op), arity_(0), langop_(0)
  { }
  ASTNode(unsigned char op, unsigned char ar)
      : opcode_(op), arity_(ar), langop_(0)
  { }

private:
  const unsigned char opcode_;
  const unsigned char arity_;    // for Construct
  unsigned short      langop_;   // for Construct
};


// Variable refers to a named variable in the current lexical scope.
class Variable : public ASTNode {
public:
  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_Variable;
  }

  Variable() = delete;
  Variable(const char* s)
      : ASTNode(AST_Variable), name_(s), index_(0)
  { }
  Variable(std::string s)
      : ASTNode(AST_Variable), name_(std::move(s)), index_(0)
  { }
  Variable(StringRef s)
      : ASTNode(AST_Variable), name_(s.str()), index_(0)
  { }

  // Name of the variable.
  const std::string& name() const { return name_; }

  // Index of the variable on the interpeter stack.
  unsigned index() const { return index_; }

  void setIndex(unsigned i) { index_ = i; }

  template <class V>
  typename V::ResultType traverse(V& visitor) {
    return visitor.reduceVariable(*this);
  }

private:
  std::string name_;
  unsigned index_;
};


// TokenStr refers to a single token in the input file.
// This class is included for completeness only, since the parser usually
// embeds tokens directly in a ParseResult.
class TokenStr : public ASTNode {
public:
  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_TokenStr;
  }

  TokenStr() = delete;
  TokenStr(const char* s)
     : ASTNode(AST_TokenStr), str_(s)
  { }
  TokenStr(std::string s)
     : ASTNode(AST_TokenStr), str_(std::move(s))
  { }
  TokenStr(StringRef s)
     : ASTNode(AST_Variable), str_(s.str())
  { }

  // Name of the variable.
  const std::string& string() const { return str_; }

  template <class V>
  typename V::ResultType traverse(V& visitor) {
    return visitor.reduceTokenStr(*this);
  }

private:
  std::string str_;
};


// Construct will construct an expression in the target language.
class Construct : public ASTNode {
public:
  static unsigned const int Max_Arity = 5;
  static unsigned const int InvalidOpcode = 0xFFFF;

  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_Construct;
  }

  Construct(const std::string &s, unsigned char arity)
      : ASTNode(AST_Construct, arity), opName_(s)
  { }

  inline ASTNode* subExpr(unsigned i);
  inline const ASTNode* subExpr(unsigned i) const;

  const std::string& opcodeName() const { return opName_; }

  template <class V>
  typename V::ResultType traverse(V& visitor) {
    unsigned nelems = arity();
    assert(nelems <= Max_Arity);
    typename V::ResultArray results(visitor, nelems);
    for (unsigned i = 0; i < nelems; ++i)
      results.add( visitor.traverse(this->subExpr(i)) );
    return visitor.reduceConstruct(*this, results);
  }

private:
  std::string opName_;
};


// Construct with a specified arity.
template <unsigned NElems>
class ConstructN : public Construct {
public:
  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_Construct;
  }

  ConstructN(const std::string& s)
     : Construct(s, 0)
  { }
  ConstructN(const std::string& s, ASTNode *E0)
     : Construct(s, 1) {
    subExprs_[0] = E0;
  }
  ConstructN(const std::string& s, ASTNode *E0, ASTNode *E1)
     : Construct(s, 2) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
  }
  ConstructN(const std::string& s, ASTNode *E0, ASTNode *E1, ASTNode *E2)
     : Construct(s, 3) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
  }
  ConstructN(const std::string& s, ASTNode *E0, ASTNode *E1, ASTNode *E2,
             ASTNode *E3)
     : Construct(s, 4) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
    subExprs_[3] = E3;
  }
  ConstructN(const std::string& s, ASTNode *E0, ASTNode *E1, ASTNode *E2,
             ASTNode *E3, ASTNode *E4)
     : Construct(s, 5) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
    subExprs_[3] = E3;
    subExprs_[4] = E4;
  }
  ~ConstructN() {
    for (unsigned i = 0; i < NElems; ++i)
      if (subExprs_[i]) delete subExprs_[i];
  }

private:
  friend class Construct;
  ASTNode* subExprs_[NElems];
};


inline ASTNode* Construct::subExpr(unsigned i) {
  return reinterpret_cast<ConstructN<1>*>(this)->subExprs_[i];
}

inline const ASTNode* Construct::subExpr(unsigned i) const {
  return reinterpret_cast<const ConstructN<1>*>(this)->subExprs_[i];
}



// EmptySeq will create an empty list.
class EmptyList : public ASTNode {
public:
  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_EmptyList;
  }

  template <class V>
  typename V::ResultType traverse(V& visitor) {
    return visitor.reduceEmptyList(*this);
  }

  EmptyList() : ASTNode(AST_EmptyList) { }
};


// Append will append an item to a list.
class Append : public ASTNode {
public:
  static bool classof(const ASTNode *e) {
    return e->opcode() == AST_Append;
  }

  Append() = delete;
  Append(ASTNode *l, ASTNode* i)
      : ASTNode(AST_Append), list_(l), item_(i)
  { }
  ~Append() {
    if (list_)
      delete list_;
    if (item_)
      delete item_;
  }

  ASTNode* list() { return list_; }
  const ASTNode* list() const { return list_; }

  ASTNode* item() { return item_; }
  const ASTNode* item() const { return item_; }

  template <class V>
  typename V::ResultType traverse(V& visitor) {
    return visitor.reduceAppend(*this, visitor.traverse(list_),
                                       visitor.traverse(item_));
  }

private:
  ASTNode* list_;
  ASTNode* item_;
};



// Generic traversal template.  R is reducer class.
//
// traverse(Node) will dispatch on the type of node and call
// node->traverse(*this).
//
// ASTNode::traverse(visitor) will invoke visitor.traverse recursively, and
// then return visitor.reduceX to generate a result.  The reduceX methods
// should be supplied by R.
//
template <class Self, class R>
class Traversal : public R {
public:
  typedef typename R::ResultType ResultType;

  Self* self() { return static_cast<Self*>(this); }

  ResultType traverse(ASTNode *node) {
    return self()->traverseASTNode(node);
  }

  ResultType traverseASTNode(ASTNode *node) {
    if (!node)
      return self()->reduceNone();

    switch (node->opcode()) {
      case ASTNode::AST_None:
        return self()->reduceNone();
      case ASTNode::AST_Variable:
        return self()->traverseVariable(cast<Variable>(node));
      case ASTNode::AST_TokenStr:
        return self()->traverseTokenStr(cast<TokenStr>(node));
      case ASTNode::AST_Construct:
        return self()->traverseConstruct(cast<Construct>(node));
      case ASTNode::AST_EmptyList:
        return self()->traverseEmptyList(cast<EmptyList>(node));
      case ASTNode::AST_Append:
        return self()->traverseAppend(cast<Append>(node));
    }
    return self()->reduceNone();
  }

  ResultType traverseVariable(Variable *node) {
    return node->traverse(*self());
  }

  ResultType traverseTokenStr(TokenStr *node) {
    return node->traverse(*self());
  }

  ResultType traverseConstruct(Construct *node) {
    return node->traverse(*self());
  }

  ResultType traverseEmptyList(EmptyList *node) {
    return node->traverse(*self());
  }

  ResultType traverseAppend(Append *node) {
    return node->traverse(*self());
  }
};


class VisitReducer {
public:
  typedef bool ResultType;

  struct ResultArray {
    ResultArray(VisitReducer& v, unsigned n) { }
    void add(bool r) { success = success && r; }
    bool success = true;
  };

  bool reduceNone() {
    return true;
  }
  bool reduceVariable(Variable &node) {
    return true;
  }
  bool reduceTokenStr(TokenStr &node) {
    return true;
  }
  bool reduceConstruct(Construct &node, ResultArray &results) {
    return results.success;
  }
  bool reduceEmptyList(EmptyList &node) {
    return true;
  }
  bool reduceAppend(Append &node, bool l, bool i) {
    return l && i;
  }
};


template <class Self>
class Visitor : public Traversal<Self, VisitReducer> {
public:
  bool traverse(ASTNode *node) {
    success = success && this->self()->traverseASTNode(node);
    return success;
  }

  bool visit(ASTNode *node) {
    success = true;
    return this->self()->traverse(node);
  }

  bool success = true;
};


class PrettyPrinter {
public:
  void print(const ASTNode* node, std::ostream &ss) ;

protected:
  void printNone(std::ostream& ss);
  void printVariable(const Variable* e, std::ostream& ss);
  void printTokenStr(const TokenStr* e, std::ostream& ss);
  void printConstruct(const Construct* e, std::ostream& ss);
  void printEmptyList(const EmptyList* e, std::ostream& ss);
  void printAppend(const Append* e, std::ostream& ss);
};



}  // end namespace parsing
}  // end namespace ast
}  // end namespace ohmu

#endif  // OHMU_SURFACE_AST_H
