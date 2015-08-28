//===- StandaloneRunner.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Convenience class providing methods to run graph computations with the LSA
// framework.
//===----------------------------------------------------------------------===/

#ifndef OHMU_LSA_STANDALONERUNNER_H
#define OHMU_LSA_STANDALONERUNNER_H

#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/CommandLine.h"
#include "lsa/GraphDeserializer.h"
#include "lsa/StandaloneGraphComputation.h"

#include <iostream>

namespace ohmu {
namespace lsa {

static llvm::cl::opt<int> NThreads("t",
                                   llvm::cl::desc("Specify number of threads"),
                                   llvm::cl::value_desc("number"),
                                   llvm::cl::Optional);

static llvm::cl::opt<std::string>
InputFile("i", llvm::cl::desc("Specify input file"),
               llvm::cl::value_desc("file"), llvm::cl::Required);

template <class UserComputation> class StandaloneRunner {
public:
  StandaloneRunner(int argc, const char *argv[]) {
    llvm::cl::ParseCommandLineOptions(argc, argv);
  }

  void readCallGraph() {
    GraphDeserializer<UserComputation>::read(InputFile.getValue(),
                                             &ComputationGraphBuilder);
  }

  void runComputation() {
    if (NThreads.getNumOccurrences() > 0)
      ComputationGraphBuilder.setNThreads(NThreads.getValue());

    ComputationGraphBuilder.run(&Factory);
  }

  void printComputationResult() {
    std::unique_ptr<ohmu::lsa::GraphComputation<UserComputation>> Computation(
        Factory.createComputation());
    for (const auto &Vertex : ComputationGraphBuilder.getVertices())
      std::cout << Vertex.id() << ": " << Computation->output(&Vertex) << "\n";
  }

private:
  ohmu::lsa::StandaloneGraphBuilder<UserComputation> ComputationGraphBuilder;
  ohmu::lsa::GraphComputationFactory<UserComputation> Factory;
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_STANDALONERUNNER_H
