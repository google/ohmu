//===- test_BNFParser.cpp --------------------------------------*- C++ --*-===//
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

#include <iostream>
#include <stdio.h>

#include "../parser/DefaultLexer.h"
#include "../parser/BNFParser.h"

using namespace ohmu::parsing;

bool validate(Parser &p) {
  // validate parser
  std::cout << "Validating parser: \n";
  // p.setTraceValidate(true);
  if (!p.init()) {
    std::cout << "Validation failed.\n";
    return false;
  }
  else {
    std::cout << "Validation succeeded.\n";
  }

  // p.setTraceValidate(true);
  std::cout << "Syntax definitions: \n";
  p.printSyntax(std::cout);
  return true;
}


int main(int argc, const char** argv) {
  ohmu::parsing::DefaultLexer lexer;
  ohmu::parsing::BNFParser bnfParser(&lexer);

  // build parser
  bnfParser.defineSyntax();
  if (!validate(bnfParser))
    return -1;

  // bootstrap parser
  std::cout << "Opening sexpr.grammar:\n";
  FILE* file = fopen("src/grammar/parser.grammar", "r");
  if (!file) {
    std::cout << "File not found.\n";
    return -1;
  }

  FileStream fs(file);
  lexer.setStream(&fs);
  auto *startRule = bnfParser.findDefinition("definitionList");

  ohmu::parsing::BNFParser bootstrapParser(&lexer);
  bnfParser.setTarget(&bootstrapParser);
  // bnfParser.setTrace(true);
  bnfParser.parse(startRule);
  fclose(file);

  if (!validate(bootstrapParser))
    return -1;

  return 0;
}


