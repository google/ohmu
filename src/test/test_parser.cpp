//===- test_parser.cpp -----------------------------------------*- C++ --*-===//
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

#include "test/Driver.h"
#include "til/Bytecode.h"
#include "til/VisitCFG.h"


using namespace ohmu;
using namespace ohmu::parsing;
using namespace ohmu::til;


void printSExpr(SExpr* e) {
  TILDebugPrinter::print(e, std::cout);
}



int main(int argc, const char** argv) {
  if (argc == 0) {
    std::cerr << "No file to parse.\n";
    return 0;
  }

  Global global;
  Driver driver;

  // Load up the ohmu grammar.
  bool success = driver.initParser("src/grammar/ohmu.grammar");
  if (!success)
    return -1;

  // Parse the ohmu source file.
  success = driver.parseDefinitions(&global, argv[1]);
  if (!success)
    return -1;

  // Convert high-level AST to low-level IR.
  global.lower();
  std::cout << "\n------ Ohmu IR ------\n";
  global.print(std::cout);

  // Find all of the CFGs.
  VisitCFG visitCFG;
  visitCFG.traverseAll(global.global());

  BytecodeWriter::write(std::cout, global.global());

  std::cout << "\n\nNumber of CFGs: " << visitCFG.cfgs().size() << "\n\n";
  return 0;
}

