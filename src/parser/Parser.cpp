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

#include "parser/Parser.h"


namespace ohmu {

namespace parsing {


bool Parser::init() {
  bool success = true;
  for (ParseRule *r : definitions_)
    success = success && r->init(*this);
  if (!success) {
    std::cerr << "\nFailed to initialize parser.\n";
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
  if (!parseError_)
    return resultStack_.getBack();
  return ParseResult();
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


bool ParseResult::append(ParseResult &&p) {
  ListType* vect;
  if (isEmpty()) {
    assert(result_ == nullptr);
    resultKind_ = p.resultKind_;
    isList_ = true;
    vect = new ListType();
    result_ = vect;
  }
  else {
    assert(isList_);
    vect = reinterpret_cast<ListType*>(result_);
  }
  if (p.isList_ || p.resultKind_ != resultKind_)
    return false;

  vect->push_back(p.result_);
  p.release();
  return true;
}


void AbstractStack::dump() {
  std::cerr << "[";
  for (unsigned i=0,n=stack_.size(); i<n; ++i) {
    if (i==blockStart_) std::cerr << "| ";
    else if (i==lexicalStart_) std::cerr << ". ";
    if (stack_[i]) std::cerr << *stack_[i] << " ";
    else std::cerr << "0 ";
  }
  if (blockStart_ == stack_.size()) std::cerr << "|";
  else if (lexicalStart_ == stack_.size()) std::cerr << ". ";
  std::cerr << "]";
}


void ResultStack::dump() {
  std::cerr << " [";
  for (unsigned i=0,n=stack_.size(); i<n; ++i) {
    if (stack_[i].isEmpty())      std::cerr << ".";
    else if (stack_[i].isToken()) std::cerr << "T";
    else if (stack_[i].isList())  std::cerr << "A";
    else std::cerr << "*";
  }
  std::cerr << "]";
}


// Small utility class to handle trace indentation.
class TraceIndenter {
public:
  TraceIndenter(Parser& p, const char* msg, const std::string* name = 0)
      : parser_(&p)
  {
    if (p.traceValidate_) {
      p.indent(std::cerr, p.traceIndent_);
      std::cerr << "--" << msg;
      if (name) std::cerr << " " << *name << " ";
      p.abstractStack_.dump();
      std::cerr << "\n";
    }
    ++p.traceIndent_;
  }
  ~TraceIndenter() { --parser_->traceIndent_; }

private:
  Parser* parser_;
};


class PrintIndenter {
public:
  PrintIndenter(Parser& p) : parser_(&p) { ++p.printIndent_; }
  ~PrintIndenter() { --parser_->printIndent_; }

private:
  Parser* parser_;
};



/*** ParseNone ***/

bool ParseNone::init(Parser& parser) {
  TraceIndenter indenter(parser, "none");

  // None doesn't know how to unwind the stack.
  if (parser.abstractStack_.lexicalSize() > 0) {
    parser.validationError() << "Sequence cannot end with none.";
    return false;
  }
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
  TraceIndenter indenter(parser, "token");

  // Tokens don't know how to unwind the stack.
  if (parser.abstractStack_.lexicalSize() > 0) {
    parser.validationError() << "Sequence cannot end with a token.";
    return false;
  }

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
      std::cout << "\n-- Matching token ["
                << parser.look().id()
                << "]: \""
                << parser.look().string()
                << "\"";
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
  out << "%" << parser.getTokenIDString(tokenID_);
}


/*** ParseKeyword ***/

bool ParseKeyword::init(Parser& parser) {
  TraceIndenter indenter(parser, "keyword");

  if (keywordStr_.length() == 0) {
    parser.validationError() << "Invalid keyword.";
    return false;
  }
  tokenID_ = parser.registerKeyword(keywordStr_);
  if (parser.traceValidate_) {
    parser.indent(std::cerr, parser.traceIndent_);
    std::cout << "-- registered keyword " << keywordStr_ << " as "
      << tokenID_ << "\n";
  }

  // Keywords don't know how to unwind the stack.
  if (parser.abstractStack_.lexicalSize() > 0) {
    parser.validationError() << "Sequence cannot end with keyword.";
    return false;
  }
  return true;
}


void ParseKeyword::prettyPrint(Parser& parser, std::ostream& out) {
  out << '"' << keywordStr_ << '"';
}


/*** ParseSequence ***/

bool ParseSequence::init(Parser& parser) {
  TraceIndenter indenter(parser, "sequence");

  if (!first_ || !second_) {
    parser.validationError() << "Invalid sequence.";
    return false;
  }

  // first_ is parsed in its own local block, so any actions at the end which
  // rewind the stack will only rewind to this point.
  unsigned localblock = parser.abstractStack_.enterLocalBlock();
  bool success = parser.initRule(first_);
  if (!success)
    return false;

  unsigned nvals = parser.abstractStack_.localSize();
  if (nvals > 1) {
    parser.validationError() << "Rule cannot return more than one value.";
    return false;
  }
  parser.abstractStack_.exitLocalBlock(localblock);

  if (hasLetName()) {
    if (nvals == 1) {
      unsigned back = parser.abstractStack_.size()-1;
      parser.abstractStack_[back] = &letName_;
    }
    else {
      parser.validationError() << "Named subrule '" << letName_
        << "does not return a value.";
      return false;
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
  TraceIndenter indenter(parser, "option");

  if (!left_ || !right_) {
    parser.validationError() << "Invalid option.";
    return false;
  }

  // Enter a new lexical scope.  Stack rewinds will only rewind back to
  // this point.
  unsigned scope = parser.abstractStack_.enterLexicalScope();

  bool success = parser.initRule(left_);
  if (!success)
    return false;
  unsigned leftSz = parser.abstractStack_.lexicalSize();

  parser.abstractStack_.rewind();
  success = parser.initRule(right_);
  if (!success)
    return false;
  unsigned rightSz = parser.abstractStack_.lexicalSize();

  parser.abstractStack_.exitLexicalScope(scope);

  if (leftSz != rightSz) {
    parser.validationError() <<
      "Different options must return the same number of results: " <<
      leftSz << "," << rightSz;
    return false;
  }

  // left and right have both rewound to here; we need to rewind to our caller.
  parser.abstractStack_.rewind();
  for (unsigned i=0; i < rightSz; ++i)
    parser.abstractStack_.push_back(0);
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
  PrintIndenter indent(parser);
  out << "\n";
  parser.indent(out, parser.printIndent_*2);
  out << "( ";

  ParseOption* opt = this;
  ParseOption* last = this;
  while (opt) {
    opt->left_->prettyPrint(parser, out);

    out << "\n";
    parser.indent(out, parser.printIndent_*2);
    out << "| ";

    last = opt;
    opt = dyn_cast<ParseOption>(opt->right_);
  }

  last->right_->prettyPrint(parser, out);

  out << "\n";
  parser.indent(out, parser.printIndent_*2);
  out << ")";
}


/*** ParseRecurseLeft ***/

bool ParseRecurseLeft::init(Parser& parser) {
  TraceIndenter indenter(parser, "recurseLeft");

  if (!base_ || !rest_) {
    parser.validationError() << "Invalid recursive rule.";
  }

  // base_ is parsed in its own local block, so any actions at the end which
  // rewind the stack will only rewind to this point.
  unsigned localblock = parser.abstractStack_.enterLocalBlock();
  bool success = parser.initRule(base_);
  if (!success)
    return false;

  unsigned nvals = parser.abstractStack_.localSize();
  if (nvals > 1) {
    parser.validationError() << "Rule cannot return more than one value.";
    return false;
  }

  if (hasLetName()) {
    if (nvals == 1) {
      unsigned back = parser.abstractStack_.size()-1;
      parser.abstractStack_[back] = &letName_;
    }
    else {
      parser.validationError() << "Named subrule '" << letName_
        << "does not return a value.";
      return false;
    }
  }

  success = parser.initRule(rest_);
  if (!success)
    return false;

  if (parser.abstractStack_.localSize() != nvals) {
    parser.validationError() << "Recursion returns wrong number of values.";
    return false;
  }
  parser.abstractStack_.exitLocalBlock(localblock);

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

  PrintIndenter indent(parser);
  out << "\n";
  parser.indent(out, parser.printIndent_*2);
  out << "( ";

  base_->prettyPrint(parser, out);
  out << "\n";
  parser.indent(out, parser.printIndent_*2);
  out << "|*[" << letName_ << "] ";
  rest_->prettyPrint(parser, out);


  out << "\n";
  parser.indent(out, parser.printIndent_*2);
  out << ") ";
}


/*** ParseNamedDefinition ***/

bool ParseNamedDefinition::init(Parser& parser) {
  TraceIndenter indenter(parser, "definition:", &name_);

  if (!rule_) {
    parser.validationError() <<
      "Syntax rule " << name_  << " has not been defined.";
    return false;
  }

  ParseNamedDefinition* existingName = parser.findDefinition(name_);
  if (!existingName) {
    parser.validationError() <<
      "Syntax rule " << name_ << " is not defined in the parser.";
    return false;
  }
  if (existingName != this) {
    parser.validationError() <<
      "Syntax rule " << name_ << " is already defined.";
    return false;
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
    std::cout << "\n-- Parsing using rule " << name_;
  }
  return rule_;
}


void ParseNamedDefinition::prettyPrint(Parser& parser,
                                       std::ostream& out) {
  out << "\n" << name_;
  if (argNames_.size() > 0) {
    out << "[";
    for (unsigned i=0,n=argNames_.size(); i<n; ++i) {
      if (i > 0) out << ",";
      out << argNames_[i];
    }
    out << "]";
  }
  out << " ::= ";
  rule_->prettyPrint(parser, out);
  out << ";\n";
}


/*** ParseReference ***/

bool ParseReference::init(Parser& parser) {
  TraceIndenter indenter(parser, "reference:", &name_);

  ParseNamedDefinition* def = parser.findDefinition(name_);
  if (!def) {
    parser.validationError() << "No syntax definition for " << name_;
    return false;
  }
  if (!definition_) {
    definition_ = def;
  } else if (definition_ != def) {
    parser.validationError() << "Inconsistent definitions for " << name_;
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

  // Drop everything in the lexical scope off of the abstract stack.
  parser.abstractStack_.rewind();

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
    parser.resultStack_.moveAndPush(frameStart + arguments_[i]);
  if (drop_ > 0)
    parser.resultStack_.drop(drop_, arguments_.size());
  return definition_;
}


void ParseReference::prettyPrint(Parser& parser, std::ostream& out) {
  out << name_;
  if (argNames_.size() > 0) {
    out << "[";
    for (unsigned i=0,n=argNames_.size(); i<n; ++i) {
      if (i > 0) out << ",";
      out << argNames_[i];
      if (parser.traceValidate_ && i < arguments_.size())
        out << "_" << arguments_[i];
    }
    out << "]";
  }
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
    unsigned op = parser_->lookupOpcode(node.opcodeName());
    if (op == ast::Construct::InvalidOpcode) {
      parser_->validationError()
        << "Cannot find opcode for " << node.opcodeName() << ".";
      return false;
    }
    node.setLangOpcode(static_cast<unsigned short>(op));
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
    return parser_->resultStack_.getElem(idx);
  }

  ParseResult reduceTokenStr(ast::TokenStr &node) {
    const char* s = node.string().c_str();
    return ParseResult(new Token(TK_None, s, SourceLocation()));
  }

  ParseResult reduceConstruct(ast::Construct &node, ResultArray& results) {
    return parser_->makeExpr(node.langOpcode(), node.arity(), results.array);
  }

  ParseResult reduceEmptyList(ast::EmptyList &node) {
    return ParseResult();   // Use null as an empty list.
  }

  ParseResult reduceAppend(ast::Append &node,
                           ParseResult &&l, ParseResult &&e) {
    bool success = l.append(std::move(e));
    if (!success) {
      parser_->parseError(SourceLocation()) <<
        "Lists must contain the same kind of node.";
    }
    return std::move(l);
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
  TraceIndenter indenter(parser, "action");

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
  bool success = visitor.traverse(node_);
  if (!success)
    return false;

  // Drop everything in the current lexical scope off of the abstract stack.
  parser.abstractStack_.rewind();

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
  out << "{ ";
  ast::PrettyPrinter printer;
  printer.print(node_, out);
  out << " }";
}


}  // end namespace parsing

}  // end namespace ohmu


