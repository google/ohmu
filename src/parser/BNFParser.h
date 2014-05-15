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

#include <unordered_map>

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
  enum BNF_Opcode {
    // ParseRules
    BNF_None = 0,
    BNF_Token,
    BNF_Keyword,
    BNF_Sequence,
    BNF_Option,
    BNF_RecurseLeft,
    BNF_Reference,
    BNF_Action,
    BNF_NamedDefinition,
    BNF_DefinitionList,

    // ASTNodes
    BNF_Variable,
    BNF_TokenStr,
    BNF_Construct,
    BNF_EmptyList,
    BNF_Append
  };

  enum BNF_Result {
    BPR_ParseRule = ParseResult::PRS_UserDefined,
    BPR_ASTNode
  };

  // Main entry point.
  // Read grammar definition from file, and use it to initialize parser.
  // If trace is true, will print out debugging information.
  static bool initParserFromFile(Parser &parser, FILE* file, bool trace=false);

public:
  BNFParser(Lexer *lexer) : Parser(lexer) {
    initMap();
  }
  ~BNFParser() { }

  // Initialize opcode dictionary.  (Dictionary used by lookupOpcode.)
  void initMap();

  // Get the name of op.  Used to construct opcode dictionary.
  const char* getOpcodeName(BNF_Opcode op);

  // Lookup the opcode for string s.
  unsigned lookupOpcode(const std::string &s) override;

  // Make a ParseRule or ASTNode.
  ParseResult makeExpr(unsigned op, unsigned arity, ParseResult *prs) override;

  // Create the default BNF grammar.
  void defineGrammar();

  // The BNF parser parses a grammar definition in BNF form,
  // and it will add all of the definitions to the target parser.
  void setTarget(Parser* p) { targetParser_ = p; }

public:
  std::unordered_map<std::string, unsigned> opcodeNameMap_;
  Parser* targetParser_ = nullptr;
};


}  // end namespace parser
}  // end namespace ohmu

#endif   // OHMU_BNF_PARSER_H


