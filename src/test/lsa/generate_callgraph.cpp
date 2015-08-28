//===- generate_callgraph.cpp ----------------------------------*- C++ --*-===//
// Simple program that generates and prints the call graph and OhmuIR of a
// single translation unit.
//===----------------------------------------------------------------------===//

#include <iostream>

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "lsa/BuildCallGraph.h"
#include "lsa/GraphSerializer.h"

static llvm::cl::opt<std::string>
    OutputFile("o", llvm::cl::desc("Specify output file"),
               llvm::cl::value_desc("file"), llvm::cl::Optional);

int main(int argc, const char *argv[]) {

  clang::tooling::CommonOptionsParser OptParser(argc, argv,
                                                llvm::cl::GeneralCategory);
  ohmu::lsa::DefaultCallGraphBuilder CallGraphBuilder;
  clang::ast_matchers::MatchFinder Finder;
  ohmu::lsa::CallGraphBuilderTool BuilderTool;
  BuilderTool.RegisterMatchers(CallGraphBuilder, &Finder);

  clang::tooling::ClangTool Tool(OptParser.getCompilations(),
                                 OptParser.getSourcePathList());

  int Res = Tool.run(clang::tooling::newFrontendActionFactory(&Finder).get());
  if (Res != 0)
    return Res;

  if (OutputFile.getNumOccurrences() > 0) {
    ohmu::lsa::GraphSerializer::write(OutputFile.getValue(), &CallGraphBuilder);
  } else {
    CallGraphBuilder.Print(std::cout);
  }

  return 0;
}
