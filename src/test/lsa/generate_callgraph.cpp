//===- generate_callgraph.cpp ----------------------------------*- C++ --*-===//
// Simple program that generates and prints the call graph and OhmuIR of a
// single translation unit.
// TODO: add a flag for writing the generated call graph and OhmuIR to files.
//===----------------------------------------------------------------------===//

#include <iostream>

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "lsa/BuildCallGraph.h"

int main(int argc, const char *argv[]) {

  clang::tooling::CommonOptionsParser OptParser(
      argc, argv, llvm::cl::GeneralCategory);
  ohmu::lsa::DefaultCallGraphBuilder CallGraphBuilder;
  clang::ast_matchers::MatchFinder Finder;
  ohmu::lsa::CallGraphBuilderTool BuilderTool;
  BuilderTool.RegisterMatchers(CallGraphBuilder, &Finder);

  clang::tooling::ClangTool Tool(OptParser.getCompilations(),
                                 OptParser.getSourcePathList());

  int Res = Tool.run(clang::tooling::newFrontendActionFactory(&Finder).get());
  if (Res != 0)
    return Res;

  CallGraphBuilder.Print(std::cout);

  return 0;
}
