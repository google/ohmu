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
#include "til/CFGReducer.h"
#include "jagger/interface.h"

#include <iostream>

using namespace ohmu;
using namespace ohmu::parsing;
using namespace clang::threadSafety;

void printSExpr(til::SExpr* e) {
  TILDebugPrinter::print(e, std::cout);
}


int main(int argc, const char** argv) {
  DefaultLexer lexer;
  TILParser tilParser(&lexer);


  const char* grammarFileName = "src/grammar/ohmu.grammar";
  FILE* file = fopen(grammarFileName, "r");
  if (!file) {
    std::cout << "File " << grammarFileName << " not found.\n";
    return -1;
  }

  bool success = BNFParser::initParserFromFile(tilParser, file, false);
  std::cout << "\n";
  // if (success)
  //   tilParser.printSyntax(std::cout);

  fclose(file);

  if (argc == 0)
    return 0;

  // Read the ohmu file.
  auto *startRule = tilParser.findDefinition("definitions");
  if (!startRule) {
    std::cout << "Grammar does not contain rule named 'definitions'.\n";
    return -1;
  }

  file = fopen(argv[1], "r");
  if (!file) {
    std::cout << "File " << argv[1] << " not found.\n";
    return -1;
  }

  std::cout << "\nParsing " << argv[1] << "...\n";
  FileStream fs(file);
  lexer.setStream(&fs);
  // tilParser.setTrace(true);
  ParseResult result = tilParser.parse(startRule);
  if (tilParser.parseError())
    return -1;

  // Pretty print the parsed ohmu code.
  auto* v = result.getList<til::SExpr>(TILParser::TILP_SExpr);
  if (!v) {
    std::cout << "No definitions found.\n";
    return 0;
  }

  for (SExpr* e : *v) {
    std::cout << "\nDefinition:\n";
    printSExpr(e);
    std::cout << "\nCFG:\n";
    SCFG* cfg = CFGRewriter::convertSExprToCFG(e, tilParser.arena());
    printSExpr(cfg);
    //encode(cfg, nullptr);
  }
  delete v;

  std::cout << "\n";
  return 0;
}

