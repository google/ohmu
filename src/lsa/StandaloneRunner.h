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

template <class UserComputation> class StandaloneRunner {
public:
  StandaloneRunner(int argc, const char *argv[])
      : OptParser(argc, argv, llvm::cl::GeneralCategory) {}

  int buildCallGraph() {
    clang::ast_matchers::MatchFinder Finder;
    ohmu::lsa::CallGraphBuilderTool BuilderTool;
    BuilderTool.RegisterMatchers(CallGraphBuilder, &Finder);

    clang::tooling::ClangTool Tool(OptParser.getCompilations(),
                                   OptParser.getSourcePathList());

    return Tool.run(clang::tooling::newFrontendActionFactory(&Finder).get());
  }

  void printCallGraph() { CallGraphBuilder.Print(std::cout); }

  void runComputation() {
    for (const auto &El : CallGraphBuilder.GetGraph()) {
      typename ohmu::lsa::GraphTraits<UserComputation>::VertexValueType Value;
      ComputationGraphBuilder.addVertex(El.first, El.second->GetIR(), Value);
      ohmu::lsa::DefaultCallGraphBuilder::CallGraphNode *Node = El.second.get();
      for (const std::string &Call : *Node->GetCalls()) {
        ComputationGraphBuilder.addEdge(El.first, Call, true);
        ComputationGraphBuilder.addEdge(Call, El.first, false);
      }
    }

    ComputationGraphBuilder.run(&Factory);
  }

  void printComputationResult() {
    std::unique_ptr<ohmu::lsa::GraphComputation<UserComputation>> Computation(
        Factory.createComputation());
    for (const auto &Vertex : ComputationGraphBuilder.getVertices())
      std::cout << Vertex.id() << ": " << Computation->output(&Vertex) << "\n";
  }

private:
  clang::tooling::CommonOptionsParser OptParser;
  ohmu::lsa::DefaultCallGraphBuilder CallGraphBuilder;
  ohmu::lsa::StandaloneGraphBuilder<UserComputation> ComputationGraphBuilder;
  ohmu::lsa::GraphComputationFactory<UserComputation> Factory;
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_STANDALONERUNNER_H
