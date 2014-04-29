//===- Parser.cpp ----------------------------------------------*- C++ --*-===//
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
// This file defines a library for constructing LL(k) parsers.  The library is
// based on the idea of "parser combinators", in which larger parsers are
// constructed from smaller ones.
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "Token.h"
#include "Lexer.h"
#include "Parser.h"


namespace ohmu {

namespace parsing {


bool Parser::init() {
  bool success = true;
  for (ParseRule *r : definitions_)
    success = success && r->init(*this);
  if (!success) {
    validationError() << "Invalid parser.";
  }
  return success;
}

// Public entry point to parsing.
ParseResult Parser::parse(ParseNamedDefinition* start) {
  if (start->numArguments() != 0) {
    parseError(SourceLocation()) << "Start rule must have no arguments";
    return ParseResult();
  }
  parseError_ = false;
  resultStack_.clear();
  parseRule(start);
  return std::move(resultStack_.back());
}


// Parse rule p.  The result will be left on the top of the stack.
void Parser::parseRule(ParseRule* rule) {
  ParseRule* nextState = rule;
  while (nextState && !parseError_) {
    nextState = nextState->parse(*this);
  }
}

// Initialize rule p.
inline bool Parser::initRule(ParseRule* p) {
  return p->init(*this);
}


void Parser::printSyntax(std::ostream& out) {
  for (unsigned i=0,n=definitions_.size(); i<n; ++i) {
    definitions_[i]->prettyPrint(*this, out);
  }
}


std::ostream& Parser::validationError() {
  parseError_ = true;
  std::cerr << "\nSyntax definition error: ";
  return std::cerr;
}


std::ostream& Parser::parseError(const SourceLocation& sloc) {
  parseError_ = true;
  std::cerr << "\nSyntax error (" <<
    sloc.lineNum << ":" << sloc.linePos << "): ";
  return std::cerr;
}


/*** ParseNone ***/

bool ParseNone::init(Parser& parser) {
  return true;
}


bool ParseNone::accepts(const Token& tok) {
  return true;
}


ParseRule* ParseNone::parse(Parser& parser) {
  return nullptr;
}


void ParseNone::prettyPrint(Parser& parser, std::ostream& out) {
  out << "null";
}


/*** ParseToken ***/

bool ParseToken::init(Parser& parser) {
  if (skip_)
    return true;
  parser.abstractStack_.push_back(nullptr);
  return true;
}


bool ParseToken::accepts(const Token& tok) {
  return (tok.id() == tokenID_);
}


ParseRule* ParseToken::parse(Parser& parser) {
  const Token& tok = parser.look();
  if (tok.id() == tokenID_) {
    if (parser.trace_) {
      std::cout << "-- Matching token ["
                << parser.look().id()
                << "]: \""
                << parser.look().string()
                << "\"\n";
    }
    if (skip_)
      parser.skip();
    else
      parser.consume();   // Pushes token onto the stack.
  }
  else {
    parser.parseError(tok.location())
      << "expecting token: "
      << parser.getTokenIDString(tokenID_)
      << " received token: "
      << parser.getTokenIDString(parser.look().id());
  }
  return 0;
}


void ParseToken::prettyPrint(Parser& parser, std::ostream& out) {
  out << "[" << parser.getTokenIDString(tokenID_) << "]";
}


/*** ParseKeyword ***/

bool ParseKeyword::init(Parser& parser) {
  if (keywordStr_.length() == 0) {
    parser.validationError() << "Invalid keyword.";
    return false;
  }
  tokenID_ = parser.registerKeyword(keywordStr_);
  if (parser.traceValidate_) {
    std::cout << "Registered keyword " << keywordStr_ << " as "
      << tokenID_ << "\n";
  }
  return true;
}


void ParseKeyword::prettyPrint(Parser& parser, std::ostream& out) {
  out << '"' << keywordStr_ << '"';
}


/*** ParseSequence ***/

bool ParseSequence::init(Parser& parser) {
  if (!first_ || !second_) {
    parser.validationError() << "Invalid sequence.";
    return false;
  }

  unsigned lsz = parser.abstractStack_.localSize();

  // First is parsed in its own local block, so any actions at the end which
  // rewind the stack will only rewind to this point.
  unsigned localblock = parser.abstractStack_.enterLocalBlock();
  bool success = parser.initRule(first_);
  parser.abstractStack_.exitLocalBlock(localblock);
  if (!success)
    return false;

  unsigned nvals = parser.abstractStack_.localSize() - lsz;
  if (nvals > 1) {
    parser.validationError() << "Rule cannot return more than one value.";
    return false;
  }

  if (hasLetName()) {
    if (nvals == 1) {
      parser.abstractStack_[lsz] = &letName_;
    }
    else {
      parser.validationError() << "Named subrule '" << letName_
        << "does not return a value.";
      // we can recover from this.
    }
  }

  success = parser.initRule(second_);
  return success;
}


bool ParseSequence::accepts(const Token& tok) {
  return first_->accepts(tok);
}


ParseRule* ParseSequence::parse(Parser& parser) {
  parser.parseRule(first_);
  return second_;   // tail call to second_
}


void ParseSequence::prettyPrint(Parser& parser, std::ostream& out) {
  if (hasLetName())
    out << letName_ << "=";
  first_->prettyPrint(parser, out);
  out << " ";
  second_->prettyPrint(parser, out);
}


/*** ParseOption ***/

bool ParseOption::init(Parser& parser) {
  if (!left_ || !right_) {
    parser.validationError() << "Invalid option.";
    return false;
  }

  unsigned initialSz = parser.abstractStack_.localSize();
  bool success = parser.initRule(left_);
  if (!success)
    return false;
  unsigned leftSz = parser.abstractStack_.localSize();

  parser.abstractStack_.rewind(initialSz);
  success = parser.initRule(right_);
  if (!success)
    return false;

  if (parser.abstractStack_.localSize() != leftSz) {
    parser.validationError() <<
      "Different options must return the same number of results.";
    return false;
  }

  // Erase any names introduced by the option.
  for (unsigned i = initialSz; i < leftSz; ++i)
    parser.abstractStack_[i] = nullptr;
  return success;
}


bool ParseOption::accepts(const Token& tok) {
  return left_->accepts(tok) || right_->accepts(tok);
}


ParseRule* ParseOption::parse(Parser& parser) {
  if (left_->accepts(parser.look()))
    return left_;   // tail call to left_
  else
    return right_;  // tail call to right_
}


void ParseOption::prettyPrint(Parser& parser, std::ostream& out) {
  out << "( ";
  left_->prettyPrint(parser, out);
  out << "\n| ";
  right_->prettyPrint(parser, out);
  out << "\n)";
}


/*** ParseRecurseLeft ***/

bool ParseRecurseLeft::init(Parser& parser) {
  if (!base_ || !rest_) {
    parser.validationError() << "Invalid recursive rule.";
  }

   unsigned lsz = parser.abstractStack_.localSize();

  // First is parsed in its own local block, so any actions at the end which
  // rewind the stack will only rewind to this point.
  unsigned localblock = parser.abstractStack_.enterLocalBlock();
  bool success = parser.initRule(base_);
  parser.abstractStack_.exitLocalBlock(localblock);
  if (!success)
    return false;

  unsigned nvals = parser.abstractStack_.localSize() - lsz;
  if (nvals > 1) {
    parser.validationError() << "Rule cannot return more than one value.";
    return false;
  }

  if (hasLetName()) {
    if (nvals == 1) {
      parser.abstractStack_[lsz] = &letName_;
    }
    else {
      parser.validationError() << "Named subrule '" << letName_
        << "does not return a value.";
      // we can recover from this.
    }
  }

  unsigned bsz = parser.abstractStack_.localSize();
  success = parser.initRule(rest_);

  if (parser.abstractStack_.localSize() != bsz) {
    parser.validationError() << "Recursion returns wrong number of values.";
    return false;
  }
  return success;
}


bool ParseRecurseLeft::accepts(const Token& tok) {
  return base_->accepts(tok);
}


ParseRule* ParseRecurseLeft::parse(Parser& parser) {
  parser.parseRule(base_);
  if (!rest_)
    return 0;

  while (rest_->accepts(parser.look()) && !parser.parseError_) {
    parser.parseRule(rest_);
  }
  return 0;
}


void ParseRecurseLeft::prettyPrint(Parser& parser, std::ostream& out) {
  if (!rest_)
    return base_->prettyPrint(parser, out);

  out << "( ";
  base_->prettyPrint(parser, out);
  out << "\n|* ";
  rest_->prettyPrint(parser, out);
  out << "\n)";
}


/*** ParseReference ***/

bool ParseReference::init(Parser& parser) {
  assert(!definition_ && "Already initialized.");

  definition_ = parser.findDefinition(name_);
  if (!definition_) {
    parser.validationError() << "No syntax definition for " << name_;
    return false;
  }

  // Calculate indices for named arguments.
  for (auto &name : argNames_) {
    unsigned idx = parser.abstractStack_.getIndex(name);
    if (idx == AbstractStack::InvalidIndex) {
      parser.validationError() << "Identifier " << name << " not found.";
      return false;
    }
    arguments_.push_back(idx);
  }

  if (arguments_.size() != definition_->numArguments()) {
    parser.validationError() <<
      "Reference to " << name_ << " has the wrong number of arguments.";
    return false;
  }

  // Argument indices are computed relative to the current stack frame.
  frameSize_ = parser.abstractStack_.size();

  // Calls which occur in a tail position are responsible for dropping items
  // off of the stack.
  drop_ = parser.abstractStack_.localSize();

  // Drop everything in the local block off of the abstract stack.
  parser.abstractStack_.rewind(0);

  // Top-level rules must return a single value.
  parser.abstractStack_.push_back(nullptr);

  return true;
}


bool ParseReference::accepts(const Token& tok) {
  return definition_->accepts(tok);
}


ParseRule* ParseReference::parse(Parser& parser) {
  unsigned frameStart = parser.resultStack_.size() - frameSize_;
  for (unsigned i=0, n=arguments_.size(); i<n; ++i)
    parser.resultStack_.moveAndPush(frameStart + i);
  if (drop_ > 0)
    parser.resultStack_.drop(drop_, arguments_.size());
  return definition_;
}


void ParseReference::prettyPrint(Parser& parser, std::ostream& out) {
  out << name_;
  if (argNames_.size() > 0) {
    out << "(";
    for (unsigned i=0,n=argNames_.size(); i<n; ++i) {
      if (i > 0) out << ",";
      out << argNames_[i];
    }
    out << ")";
  }
}


/*** ParseNamedDefinition ***/

bool ParseNamedDefinition::init(Parser& parser) {
  if (!rule_) {
    parser.validationError() <<
      "Syntax rule " << name_  << " has not been defined.\n";
    return false;
  }

  ParseNamedDefinition* existingName = parser.findDefinition(name_);
  if (existingName != this) {
    parser.validationError() <<
      "Syntax rule " << name_ << " is already defined.";
    return false;
  }

  if (parser.traceValidate_) {
    std::cout << "-- Validating rule: " << name_ << "\n";
  }

  // Push arguments onto stack.
  parser.abstractStack_.clear();
  for (unsigned i=0, n=argNames_.size(); i<n; ++i) {
    parser.abstractStack_.push_back(&argNames_[i]);
  }
  bool success = parser.initRule(rule_);

  if (parser.abstractStack_.size() != 1) {
    parser.validationError()
      << "A top-level named definition must return a result.";
  }
  return success;
}


bool ParseNamedDefinition::accepts(const Token& tok) {
  return rule_->accepts(tok);
}


ParseRule* ParseNamedDefinition::parse(Parser& parser) {
  if (parser.trace_) {
    std::cout << "-- Parsing using rule " << name_ << "\n";
  }
  return rule_;
}


void ParseNamedDefinition::prettyPrint(Parser& parser,
                                       std::ostream& out) {
  out << name_;
  if (argNames_.size() > 0) {
    out << "(";
    for (unsigned i=0,n=argNames_.size(); i<n; ++i) {
      if (i > 0) out << ",";
      out << argNames_[i];
    }
    out << ")";
  }
  out << " ::= \n";
  rule_->prettyPrint(parser, out);
  out << ";\n";
}



/*** ParseAction ***/

class ASTIndexVisitor : public ast::Visitor<ASTIndexVisitor> {
public:
  ASTIndexVisitor(Parser *p) : parser_(p) { }

  bool reduceVariable(ast::Variable &node) {
    unsigned idx = parser_->abstractStack_.getIndex(node.name());
    if (idx == AbstractStack::InvalidIndex) {
      parser_->validationError() <<
        "Identifier " << node.name() << " not found.";
      return false;
    }
    node.setIndex(idx);
    return true;
  }

  bool reduceConstruct(ast::Construct &node, ResultArray& results) {
    unsigned op = parser_->getLanguageOpcode(node.opcodeName());
    assert(op < 0xFFFF && "Invalid opcode");
    node.setLangOpcode(op);
    return true;
  }

private:
  Parser* parser_;
};


class ASTInterpretReducer {
public:
  typedef ParseResult ResultType;

  struct ResultArray {
    ResultArray(ASTInterpretReducer& r, unsigned n) { }

    void add(ParseResult &&pr) { array[i++] = std::move(pr); }

    unsigned i = 0;
    ParseResult array[ast::Construct::Max_Arity];
  };

  ParseResult reduceNone() {
    return ParseResult();
  }

  ParseResult reduceVariable(ast::Variable &node) {
    unsigned idx = frameStart_ + node.index();
    return std::move(parser_->resultStack_[idx]);
  }

  ParseResult reduceTokenStr(ast::TokenStr &node) {
    const char* s = node.string().c_str();
    return ParseResult(Token(TK_None, s, SourceLocation()));
  }

  ParseResult reduceConstruct(ast::Construct &node, ResultArray& results) {
    return parser_->makeExpr(node.langOpcode(), node.arity(), results.array);
  }

  ParseResult reduceEmptyList(ast::EmptyList &node) {
    // null can be used as an empty list.
    return ParseResult(static_cast<ParseResult::ListType*>(nullptr));
  }

  ParseResult reduceAppend(ast::Append &node,
                           ParseResult &&l, ParseResult &&e) {
    ParseResult::ListType *lst = l.getASTNodeList();   // consumes l
    lst->push_back(e.getASTNode());                    // consumes e
    return ParseResult(lst);
  }

  ASTInterpretReducer() : parser_(nullptr), frameStart_(0) { }

protected:
  Parser*  parser_;
  unsigned frameStart_;
};


class ASTInterpreter : public ast::Traversal<ASTInterpreter,
                                             ASTInterpretReducer> {
public:
  ASTInterpreter(Parser *p, unsigned fs) {
    parser_ = p;
    frameStart_ = fs;
  }
};


bool ParseAction::init(Parser& parser) {
  if (!node_) {
    parser.validationError() << "Invalid action.";
    return false;
  }

  // Argument indices are computed relative to the current stack frame.
  frameSize_ = parser.abstractStack_.size();

  // Actions which occur in a tail position are responsible for dropping
  // items off the stack.
  drop_ = parser.abstractStack_.localSize();

  ASTIndexVisitor visitor(&parser);
  visitor.traverse(node_);

  // Drop everything in the local block off of the abstract stack.
  parser.abstractStack_.rewind(0);

  // Actions will return a single value.
  parser.abstractStack_.push_back(nullptr);
  return true;
}

bool ParseAction::accepts(const Token& tok) {
  return true;
}


ParseRule* ParseAction::parse(Parser& parser) {
  unsigned frameStart = parser.resultStack_.size() - frameSize_;

  ASTInterpreter interpreter(&parser, frameStart);
  parser.resultStack_.push_back(interpreter.traverse(node_));
  if (drop_ > 0)
    parser.resultStack_.drop(drop_, 1);
  return nullptr;
}

void ParseAction::prettyPrint(Parser& parser, std::ostream& out) {
  // TODO
  out << "{ ... }";
}


}  // end namespace parsing

}  // end namespace ohmu


