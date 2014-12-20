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
#include "til/VisitCFG.h"
#include "backend/jagger/types.h"


using namespace ohmu;
using namespace ohmu::parsing;
using namespace ohmu::til;

namespace Core {
extern void emitEvents(Global& global);
}

void printSExpr(SExpr* e) {
  TILDebugPrinter::print(e, std::cout);
}



int main(int argc, const char** argv) {
  if (argc == 1) {
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
  //return 0;

  // Find all of the CFGs.
  //VisitCFG visitCFG;
  //visitCFG.traverseAll(global.global());

  //std::cout << "\n\nNumber of CFGs: " << visitCFG.cfgs().size() << "\n\n";

  Core::emitEvents(global);

  return 0;
}

