#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "lsa/BuildCallGraph.h"

#include <iostream>

int main(int argc, const char *argv[]) {

  clang::tooling::CommonOptionsParser OptParser(argc, argv,
                                                llvm::cl::GeneralCategory);

  ohmu::lsa::DefaultCallGraphBuilder Builder;
  clang::ast_matchers::MatchFinder Finder;
  ohmu::lsa::CallGraphBuilderTool BuilderTool;
  BuilderTool.RegisterMatchers(Builder, &Finder);

  clang::tooling::ClangTool Tool(OptParser.getCompilations(),
                                 OptParser.getSourcePathList());

  int res = Tool.run(clang::tooling::newFrontendActionFactory(&Finder).get());
  if (res != 0)
    return res;

  std::cout << "Graph created.\n";

  Builder.Print(std::cout);

  return 0;
}
