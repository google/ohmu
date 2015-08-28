//===- escape_analysis.cpp -------------------------------------*- C++ --*-===//
//
//===----------------------------------------------------------------------===//

#include "lsa/examples/EscapeAnalysis.h"
#include "lsa/StandaloneRunner.h"

int main(int argc, const char *argv[]) {

  ohmu::lsa::StandaloneRunner<ohmu::lsa::EscapeAnalysis> Runner(argc, argv);

  Runner.readCallGraph();
  Runner.runComputation();
  Runner.printComputationResult(true);

  return 0;
}
