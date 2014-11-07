//===- ASTNode.cpp ---------------------------------------------*- C++ --*-===//
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

#include "parser/ASTNode.h"


namespace ohmu {
namespace parsing {
namespace ast {


void PrettyPrinter::print(const ASTNode* node, std::ostream &ss) {
  switch (node->opcode()) {
    case ASTNode::AST_None:
      printNone(ss);
      return;
    case ASTNode::AST_Variable:
      printVariable(cast<Variable>(node), ss);
      return;
    case ASTNode::AST_TokenStr:
      printTokenStr(cast<TokenStr>(node), ss);
      return;
    case ASTNode::AST_Construct:
      printConstruct(cast<Construct>(node), ss);
      return;
    case ASTNode::AST_EmptyList:
      printEmptyList(cast<EmptyList>(node), ss);
      return;
    case ASTNode::AST_Append:
      printAppend(cast<Append>(node), ss);
      return;
  }
}

void PrettyPrinter::printNone(std::ostream& ss) {
  ss << "null";
}

void PrettyPrinter::printVariable(const Variable* e, std::ostream& ss) {
  ss << e->name();
}

void PrettyPrinter::printTokenStr(const TokenStr* e, std::ostream& ss) {
  ss << "\"";
  ss << e->string();
  ss << "\"";
}

void PrettyPrinter::printConstruct(const Construct* e, std::ostream& ss) {
  ss << "(" << e->opcodeName();
  for (unsigned i = 0; i < e->arity(); ++i) {
    ss << " ";
    print(e->subExpr(i), ss);
  }
  ss << ")";
}

void PrettyPrinter::printEmptyList(const EmptyList* e, std::ostream& ss) {
  ss << "[]";
}

void PrettyPrinter::printAppend(const Append* e, std::ostream& ss) {
  ss << "(append ";
  print(e->list(), ss);
  ss << " ";
  print(e->item(), ss);
  ss << ")";
}



}  // namespace ohmu
}  // namespace parsing
}  // namespace ast
