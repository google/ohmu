//===- test_lsa_global_vars.cpp --------------------------------*- C++ --*-===//
// Runs the LSA call graph generation on a clang compiled file and computes
// which functions modify global variables.
//===----------------------------------------------------------------------===//

#include "lsa/examples/ExampleOhmuComputation.h"
#include "lsa/StandaloneRunner.h"

int main(int argc, const char *argv[]) {

  ohmu::lsa::StandaloneRunner<ohmu::lsa::OhmuComputation> Runner(argc, argv);

  Runner.readCallGraph();
  Runner.runComputation();
  Runner.printComputationResult();

  return 0;
}
