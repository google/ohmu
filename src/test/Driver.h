//===- Driver.h ------------------------------------------------*- C++ --*-===//
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
// Driver provides a simple harness for parsing and compiling an ohmu program
// which is shared between test cases.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_TEST_DRIVER_H
#define OHMU_TEST_DRIVER_H


#include "parser/DefaultLexer.h"
#include "parser/BNFParser.h"
#include "parser/TILParser.h"
#include "til/TIL.h"
#include "til/TILTraverse.h"
#include "til/TILCompare.h"
#include "til/CFGReducer.h"
#include "til/Global.h"

#include <iostream>

namespace ohmu {

using namespace ohmu::parsing;
using namespace ohmu::til;


class Driver {
public:
  bool initParser(FILE* grammarFile);
  bool initParser(const char* grammarFileName);

  bool parseDefinitions(Global *global, FILE *file);
  bool parseDefinitions(Global *global, const char* fname);

  Driver() : tilParser(&lexer), startRule(nullptr) { }

private:
  DefaultLexer lexer;
  TILParser    tilParser;
  ParseNamedDefinition* startRule;
};


bool Driver::initParser(FILE* grammarFile) {
  // Build the ohmu parser from the grammar file.
  bool success = BNFParser::initParserFromFile(tilParser, grammarFile, false);
  if (!success)
    return false;

  // Find the starting point.
  startRule = tilParser.findDefinition("definitions");
  if (!startRule) {
    std::cout << "Grammar does not contain rule named 'definitions'.\n";
    return false;
  }
  return true;
}


bool Driver::initParser(const char* grammarFileName) {
  // Open the grammar file.
  FILE* grammarFile = fopen(grammarFileName, "r");
  if (!grammarFile) {
    std::cout << "File " << grammarFileName << " not found.\n";
    return false;
  }
  bool success = initParser(grammarFile);
  fclose(grammarFile);
  return success;
}



bool Driver::parseDefinitions(Global *global, FILE *file) {
  // Make sure we parse results into the proper global arenas.
  tilParser.setArenas(global->StringArena, global->ParseArena);

  // Parse file.
  FileStream fs(file);
  lexer.setStream(&fs);
  // tilParser.setTrace(true);
  ParseResult result = tilParser.parse(startRule);
  if (tilParser.parseError())
    return false;

  // Add parsed definitions to global namespace.
  auto* v = result.getList<SExpr>(TILParser::TILP_SExpr);
  if (!v) {
    std::cout << "No definitions found.\n";
    return false;
  }
  global->addDefinitions(*v);
  delete v;
  return true;
}


bool Driver::parseDefinitions(Global *global, const char* fname) {
  FILE* file = fopen(fname, "r");
  if (!file) {
    std::cout << "File " << fname << " not found.\n";
    return false;
  }
  bool success = parseDefinitions(global, file);
  fclose(file);
  return success;
}

}  // end namespace ohmu


#endif  // OHMU_TEST_DRIVER_H
