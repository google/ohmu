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

int main(int argc, const char** argv) {
  ohmu::parsing::DefaultLexer lexer;
  ohmu::parsing::BNFParser bnfParser(&lexer);

  // build parser
  bnfParser.defineSyntax();
  std::cout << "Syntax definitions: \n";
  bnfParser.printSyntax(std::cout);

  // validate parser
  std::cout << "Validating parser: \n";
  // bnfParser.setTraceValidate(true);
  if (!bnfParser.init()) {
    std::cout << "Validation failed.\n";
    return -1;
  }
  else {
    std::cout << "Validation succeeded.\n";
  }

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
  bnfParser.setTrace(true);
  bnfParser.parse(startRule);
  fclose(file);

  std::cout << "Bootstrap syntax definitions: \n";
  bootstrapParser.printSyntax(std::cout);

  std::cout << "Validating bootstrap parser: \n";
  if (!bootstrapParser.init()) {
    std::cout << "Validation failed.\n";
    return -1;
  }
  else {
    std::cout << "Validation succeeded.\n";
  }
}


