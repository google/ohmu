//===- ParserBuilder.h -----------------------------------------*- C++ --*-===//
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
// Define a C++ Domain Specific Language for creating parsers.
// ParseBuilder is a wrapper class that uses operator overloading to
// make the task of building parsers simpler.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_PARSERBUILDER_H
#define OHMU_PARSERBUILDER_H

#include <cstddef>

#include "ASTNode.h"
#include "Parser.h"


namespace ohmu {

namespace parsing {


class ParseBuilder {
public:
  explicit ParseBuilder(ParseRule* r) : rule_(r) { }
  ParseBuilder(const ParseBuilder& b) : rule_(b.rule_) { }

  ParseRule* getRule() const { return rule_; }

  // Create sequence
  ParseBuilder operator>>=(const ParseBuilder& p) {
    return ParseBuilder(new ParseSequence("", getRule(), p.getRule()));
  }

  // Create option
  ParseBuilder operator|=(const ParseBuilder& p) {
    return ParseBuilder(new ParseOption(getRule(), p.getRule()));
  }

  // Create left-recursive rule
  ParseBuilder operator^=(const ParseBuilder& p) {
    return ParseBuilder(new ParseRecurseLeft("", getRule(), p.getRule()));
  }

protected:
  ParseRule* rule_;

private:
  ParseBuilder& operator=(const ParseBuilder& r) = delete;
};


// Assign a name to the first value in a sequence.
class PLet {
public:
  PLet(const char* name, const ParseBuilder& b)
      : letName_(name), builder_(b)
  { }

  ParseBuilder operator>>=(const ParseBuilder& p) {
    return ParseBuilder(
      new ParseSequence(letName_, builder_.getRule(), p.getRule()));
  }

  ParseBuilder operator^=(const ParseBuilder& p) {
    return ParseBuilder(
       new ParseRecurseLeft(letName_, builder_.getRule(), p.getRule()));
  }

private:
  const char* letName_;
  ParseBuilder builder_;
};


// Create a named definition.
class PNamedRule : public ParseBuilder {
public:
  PNamedRule(Parser* parser, const char* s)
      : ParseBuilder(new ParseNamedDefinition(s)) {
    parser->addDefinition(definition());
  }

  // Set argument.  Use in conjunction with %=
  // E.g  myDef["a"]["b"] %= ...
  PNamedRule& operator[](const std::string& s) {
    definition()->addArgument(s);
    return *this;
  }

  // Set definition
  void operator%=(const ParseBuilder& p) {
    definition()->setDefinition(p.getRule());
  }

  ParseBuilder ref() {
    return ParseBuilder(new ParseReference(definition()));
  }

  ParseBuilder ref(std::string a0) {
    auto *r = new ParseReference(definition());
    r->addArgument(a0);
    return ParseBuilder(r);
  }

  ParseBuilder ref(std::string a0, std::string a1) {
    auto *r = new ParseReference(definition());
    r->addArgument(a0);
    r->addArgument(a1);
    return ParseBuilder(r);
  }

  ParseBuilder ref(std::string a0, std::string a1, std::string a2) {
    auto *r = new ParseReference(definition());
    r->addArgument(a0);
    r->addArgument(a1);
    r->addArgument(a2);
    return ParseBuilder(r);
  }

  ParseNamedDefinition* definition() {
    return static_cast<ParseNamedDefinition*>(rule_);
  }
};


// Empty rule.
class PNone : public ParseBuilder {
public:
  PNone() : ParseBuilder(new ParseNone()) { }
};



// Parse a token, and push it on the stack
class PToken : public ParseBuilder {
public:
  PToken(unsigned tid)
    : ParseBuilder(new ParseToken(tid, false))
  { }
};


// Parse a keyword.
class PKeyword : public ParseBuilder {
public:
  PKeyword(const char* s) : ParseBuilder(new ParseKeyword(s)) { }
};


// Return a result that constructs something.
class PReturn : public ParseBuilder {
public:
  PReturn(ast::ASTNode* e)
    : ParseBuilder(new ParseAction(e))
  { }
  PReturn(const char* f)
    : ParseBuilder(new ParseAction(new ast::ConstructN<0>(f)))
  { }
  PReturn(const char* f, const char* a0)
    : ParseBuilder(new ParseAction(new ast::ConstructN<1>(f, arg(a0))))
  { }
  PReturn(const char* f, const char* a0, const char* a1)
    : ParseBuilder(new ParseAction(new ast::ConstructN<2>(f, arg(a0),
                                                             arg(a1))))
  { }
  PReturn(const char* f, const char* a0, const char* a1, const char* a2)
    : ParseBuilder(new ParseAction(new ast::ConstructN<3>(f, arg(a0),
                                                             arg(a1),
                                                             arg(a2))))
  { }

protected:
  static ast::ASTNode* arg(const char* s) {
    if (s)
      return new ast::Variable(s);
    else
      return new ast::EmptyList();
  }
};


// Return a result that is a single variable.
class PReturnVar : public PReturn {
public:
  PReturnVar(const char* s) : PReturn(new ast::Variable(s)) { }
};


class PReturnAppend : public PReturn {
public:
  PReturnAppend(const char* as, const char* a)
    : PReturn(new ast::Append(arg(as), arg(a)))
  { }
};


}  // end namespace parsing
}  // end namespace ohmu

#endif  // OHMU_PARSERBUILDER_H
