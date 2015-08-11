//===- test_lsa.cpp --------------------------------------------*- C++ --*-===//
// Runs the LSA call graph generation on a clang compiled file. To call this
// binary, use run_test_lsa.sh which sets the right include path for clang
// libraries. The clang tool requires the json compilation database to be
// present in the directory of the specified source file or a parent directory
// of the source file. Alternatively, you can specify the path to the folder
// containing the database with -p (eg: "-p=.").
//
// To create the database, specify "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" when
// running CMake, or "-t compdb" when running ninja.
//===----------------------------------------------------------------------===//

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
