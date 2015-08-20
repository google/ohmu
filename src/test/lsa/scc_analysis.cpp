//===- test_lsa_scc.cpp ----------------------------------------*- C++ --*-===//
// Runs the LSA call graph generation on a clang compiled file. To call this
// binary, use run_test_lsa.sh which sets the right include path for clang
// libraries. The clang tool requires the json compilation database to be
// present in the directory of the specified source file or a parent directory
// of the source file. Alternatively, you can specify the path to the folder
// containing the database with -p (eg: "-p=.").
//
// To create the database, specify "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" when
// running CMake, or "-t compdb" when running ninja.
//
// In addition, since clang requires some substitute includes for common system
// headers, use the exported shell script for a more convenient call:
//  $ export LLVM_BUILD=/path/to/llvm/with/clang/build/
//  $ ./src/test/run_test_lsa.sh -p=. <file>
//===----------------------------------------------------------------------===//

#include "lsa/examples/SCCComputation.h"
#include "lsa/StandaloneRunner.h"

int main(int argc, const char *argv[]) {

  ohmu::lsa::StandaloneRunner<ohmu::lsa::SCCComputation> Runner(argc, argv);

  int Res = Runner.buildCallGraph();
  if (Res != 0)
    return Res;

  Runner.runComputation();
  Runner.printComputationResult();

  return 0;
}
