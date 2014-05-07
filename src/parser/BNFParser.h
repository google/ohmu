//===- BNFParser.h ---------------------------------------------*- C++ --*-===//
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
// BNFParser is a concrete parser.  It parses grammar files in BNF form, and 
// will construct other parsers from them.
//
//===----------------------------------------------------------------------===//


#include "ASTNode.h"
#include "DefaultLexer.h"
#include "Parser.h"
#include "ParserBuilder.h"


#ifndef OHMU_BNF_PARSER_H
#define OHMU_BNF_PARSER_H

namespace ohmu {
namespace parsing {


class BNFParser : public Parser {
public:
  BNFParser(Lexer *lexer) : Parser(lexer) { }
  ~BNFParser() { }

  void defineSyntax();
  
  ParseResult makeExpr(unsigned op, unsigned arity, ParseResult *prs) override {
	return ParseResult();
  }
  
  unsigned getLanguageOpcode(const std::string &s) override { 
	return 0;
  }
};


}  // end namespace parser
}  // end namespace ohmu

#endif   // OHMU_BNF_PARSER_H


