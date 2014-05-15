//===- test_TILParser.cpp --------------------------------------*- C++ --*-===//
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

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "parser/DefaultLexer.h"
#include "parser/BNFParser.h"
#include "parser/TILParser.h"

#include <iostream>

using namespace ohmu;
using namespace ohmu::parsing;
using namespace clang::threadSafety;

class TILPrinter : public til::PrettyPrinter<TILPrinter, std::ostream> {};

void printSCFG(til::SCFG* cfg) {
  TILPrinter::print(cfg, std::cerr);
}


int main(int argc, const char** argv) {
  DefaultLexer lexer;
  TILParser tilParser(&lexer);

  FILE* file = fopen("src/grammar/ohmu.grammar", "r");
  if (!file) {
    std::cout << "File not found.\n";
    return -1;
  }

  bool success = BNFParser::initParserFromFile(tilParser, file, false);
  std::cout << "\n";
  if (success)
    tilParser.printSyntax(std::cout);

  fclose(file);
  return 0;
}

