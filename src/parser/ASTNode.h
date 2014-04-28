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
//   language.  (A concrete parser must provide a method for interpreting
//   such commands.)
// * The language also has rudimentary support for creating lists of
//   expressions, since that is a frequent parsing task.
//   A list is either [], the empty list, or (append list item)
//
// x                                            // parser variable x
// (integer "1234")                             // create integer literal
// (apply (identifier "foo") (identifier "y"))  // create expr  'foo(y)'
// (record (append [] (slot "bar" (...))))      // create record from slot list
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_SURFACE_AST_H
#define OHMU_SURFACE_AST_H

#include "Token.h"


namespace ohmu {
namespace parser {
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

  inline Opcode opcode() const {
    return static_cast<Opcode>(opcode_);
  }

  // The arity of this expression.  (Should be 0 except for Construct)
  inline unsigned char arity() const { return arity_; }

  // The target language opcode for this expression.  (for Construct)
  inline unsigned short langOpcode() const { return langop_; }

protected:
  ASTNode() = delete;
  ASTNode(unsigned char op)
      : opcode_(op), arity_(0), langop_(0)
  { }
  ASTNode(unsigned char op, unsigned char ar, unsigned short lop)
      : opcode_(op), arity_(ar), langop_(lop)
  { }

private:
  const unsigned char opcode_;
  const unsigned char arity_;    // for Construct
  unsigned short      langop_;   // for Construct
};


// Variable refers to a named variable in the current lexical scope.
class Variable : public ASTNode {
public:
  static bool classof(const ASTNode *E) {
    return E->opcode() == AST_Variable;
  }

  Variable() = delete;
  Variable(StringRef n)
      : ASTNode(AST_Variable), name_(n), index_(0)
  { }

  // Name of the variable.
  StringRef name() const { return name_; }

  // Index of the variable on the interpeter stack.
  unsigned index() const { return index_; }

  void setIndex(unsigned i) { index_ = i; }

private:
  StringRef name_;
  unsigned index_;
};


// TokenStr refers to a single token in the input file.
// This class is included for completeness only, since the parser usually
// embeds tokens directly in a ParseResult.
class TokenStr : public ASTNode {
public:
  static bool classof(const ASTNode *E) {
    return E->opcode() == AST_TokenStr;
  }

  TokenStr() = delete;
  TokenStr(StringRef s) : ASTNode(AST_TokenStr), str_(s) { }

  // Name of the variable.
  StringRef string() const { return str_; }

private:
  StringRef str_;
};


// Construct will construct an expression in the target language.
template <unsigned NElems>
class ConstructN : public ASTNode {
public:
  static bool classof(const ASTNode *E) {
    return E->opcode() == AST_Construct;
  }

  ConstructN(unsigned short lop)
     : ASTNode(AST_Construct, 0, lop)
  { }
  ConstructN(unsigned short lop, ASTNode *E0)
     : ASTNode(AST_Construct, 1, lop) {
    subExprs_[0] = E0;
  }
  ConstructN(unsigned short lop, ASTNode *E0, ASTNode *E1)
     : ASTNode(AST_Construct, 2, lop) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
  }
  ConstructN(unsigned short lop, ASTNode *E0, ASTNode *E1, ASTNode *E2)
     : ASTNode(AST_Construct, 3, lop) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
  }
  ConstructN(unsigned short lop, ASTNode *E0, ASTNode *E1, ASTNode *E2,
             ASTNode *E3)
     : ASTNode(AST_Construct, 4, lop) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
    subExprs_[3] = E3;
  }
  ConstructN(unsigned short lop, ASTNode *E0, ASTNode *E1, ASTNode *E2,
             ASTNode *E3, ASTNode *E4)
     : ASTNode(AST_Construct, 5, lop) {
    subExprs_[0] = E0;
    subExprs_[1] = E1;
    subExprs_[2] = E2;
    subExprs_[3] = E3;
    subExprs_[4] = E4;
  }

private:
  ASTNode* subExprs_[NElems];
};

typedef ConstructN<0> Construct;


// EmptySeq will create an empty list.
class EmptyList : public ASTNode {
public:
  static bool classof(const ASTNode *E) {
    return E->opcode() == AST_EmptyList;
  }

  EmptyList() : ASTNode(AST_EmptyList) { }
};


// Append will append an item to a list.
class Append : public ASTNode {
public:
  static bool classof(const ASTNode *E) {
    return E->opcode() == AST_Append;
  }

  Append() = delete;
  Append(ASTNode *l, ASTNode* i)
      : ASTNode(AST_Append), list_(l), item_(i)
  { }

  ASTNode* list() { return list_; }
  const ASTNode* list() const { return list_; }

  ASTNode* item() { return item_; }
  const ASTNode* item() const { return item_; }

private:
  ASTNode* list_;
  ASTNode* item_;
};


}  // end namespace parser
}  // end namespace ast
}  // end namespace ohmu

#endif  // OHMU_SURFACE_AST_H
