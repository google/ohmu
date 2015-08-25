//===- StandaloneRunner.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Convenience class providing methods to build the call graph and run graph
// computations with the LSA framework.
//===----------------------------------------------------------------------===/

#ifndef OHMU_LSA_STANDALONERUNNER_H
#define OHMU_LSA_STANDALONERUNNER_H

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "lsa/BuildCallGraph.h"
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
  StandaloneRunner(int argc, const char *argv[]) {}

  void loadCallGraph() {
    ohmu::til::BytecodeFileReader ReadStream(InputFile.getValue(),
        ohmu::MemRegionRef(Arena));
    int32_t NFunc = ReadStream.readInt32();
    for (unsigned i = 0; i < NFunc; i++) {
      std::string Function = ReadStream.readString();
      std::string OhmuIR = ReadStream.readString();
      typename GraphTraits<UserComputation>::VertexValueType Value;
      ComputationGraphBuilder.addVertex(Function, OhmuIR, Value);
      int32_t NNodes = ReadStream.readInt32();
      for (unsigned n = 0; n < NNodes; n++) {
        std::string Call = ReadStream.readString();
        ComputationGraphBuilder.addCall(Function, Call);
      }
    }
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
  ohmu::MemRegion Arena;
  ohmu::lsa::StandaloneGraphBuilder<UserComputation> ComputationGraphBuilder;
  ohmu::lsa::GraphComputationFactory<UserComputation> Factory;
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_STANDALONERUNNER_H
