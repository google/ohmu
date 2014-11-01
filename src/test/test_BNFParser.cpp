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

#include "parser/DefaultLexer.h"
#include "parser/BNFParser.h"
#include "parser/TILParser.h"

using namespace ohmu::parsing;

int bootstrapBNF() {
  const char* fname = "src/grammar/parser.grammar";

  ohmu::parsing::DefaultLexer lexer;
  ohmu::parsing::BNFParser bootstrapBNFParser(&lexer);

  FILE* file = fopen(fname, "r");
  if (!file) {
    std::cout << "File '" << fname << "' not found.\n";
    return -1;
  }

  BNFParser::initParserFromFile(bootstrapBNFParser, file, false);
  bootstrapBNFParser.printSyntax(std::cout);

  fclose(file);
  return 0;
}

int makeTILParser(const char* fname) {
  ohmu::parsing::DefaultLexer lexer;
  ohmu::parsing::TILParser myParser(&lexer);

  FILE* file = fopen(fname, "r");
  if (!file) {
    std::cout << "File '" << fname << "' not found.\n";
    return -1;
  }

  BNFParser::initParserFromFile(myParser, file, false);
  myParser.printSyntax(std::cout);

  fclose(file);
  return 0;
}


int main(int argc, const char** argv) {
  if (argc <= 1)
    return bootstrapBNF();
  else
    return makeTILParser(argv[1]);
}

