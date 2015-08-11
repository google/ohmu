//===- BuildCallGraph.cpp --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Til/Bytecode.h"
#include "clang/Analysis/Til/ClangCFGWalker.h"
#include "clang/Analysis/Til/ClangTranslator.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
#include "BuildCallGraph.h"

#include <memory>

namespace {

/// Return the c++ mangled name for this declaration.
std::string getMangledName(const clang::NamedDecl &D) {
  std::string Buffer;
  llvm::raw_string_ostream Out(Buffer);
  clang::MangleContext *Mc = D.getASTContext().createMangleContext();

  if (const auto *CD = llvm::dyn_cast<clang::CXXConstructorDecl>(&D))
    Mc->mangleCXXCtor(CD, clang::Ctor_Base, Out);
  else if (const auto *DD = llvm::dyn_cast<clang::CXXDestructorDecl>(&D))
    Mc->mangleCXXDtor(DD, clang::Dtor_Base, Out);
  else
    Mc->mangleName(&D, Out);

  return Out.str();
}

/// Callback that builds the CFG for each function it is called on. Reports
/// the Ohmu IR translation of that CFG as well as the calls made from that
/// function to the provided GraphConstructor.
class ExtendCallGraph : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit ExtendCallGraph(ohmu::lsa::CallGraphBuilder &C) : Builder(C) {}

  void
  run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  /// Traverses the CFG for calls to functions, constructors and destructors.
  void DiscoverCallGraph(const std::string &FName, clang::ASTContext &Ctxt,
                         clang::CFG *CFG);

  /// Generates the Ohmu IR of the function.
  void GenerateOhmuIR(const std::string &FName, const clang::Decl *Fun,
                      clang::AnalysisDeclContext &AC);

private:
  ohmu::lsa::CallGraphBuilder &Builder;
};

void ExtendCallGraph::run(
    const clang::ast_matchers::MatchFinder::MatchResult &Result) {
  const clang::FunctionDecl *Fun =
      Result.Nodes.getNodeAs<clang::FunctionDecl>("decl");

  if (!Fun->isThisDeclarationADefinition())
    return;

  if (Fun->isDependentContext())
    return;

  std::string FName = getMangledName(*Fun);
  clang::AnalysisDeclContextManager ADCM(true, true, true, true, true, true);
  clang::AnalysisDeclContext AC(&ADCM, Fun, ADCM.getCFGBuildOptions());

  GenerateOhmuIR(FName, Fun, AC);
  DiscoverCallGraph(FName, Fun->getASTContext(), AC.getCFG());
}

void ExtendCallGraph::DiscoverCallGraph(const std::string &FName,
                                        clang::ASTContext &Ctxt,
                                        clang::CFG *CFG) {
  if (CFG == nullptr)
    return;
  for (auto CFGBlockIter = CFG->begin(), E = CFG->end(); CFGBlockIter != E;
       ++CFGBlockIter) {
    for (auto CFGElementIter = (*CFGBlockIter)->begin(),
              BE = (*CFGBlockIter)->end();
         CFGElementIter != BE; ++CFGElementIter) {
      const clang::NamedDecl *Call = nullptr;

      if (clang::Optional<clang::CFGStmt> stmt =
              CFGElementIter->getAs<clang::CFGStmt>()) {
        clang::Stmt *S = const_cast<clang::Stmt *>(stmt->getStmt());

        if (const auto *CallE = llvm::dyn_cast<clang::CallExpr>(S)) {
          if (CallE->getDirectCallee())
            Call = CallE->getDirectCallee();

        } else if (const auto *ConsE =
                       llvm::dyn_cast<clang::CXXConstructExpr>(S)) {
          Call = ConsE->getConstructor();
        }

      } else if (auto ImplD = CFGElementIter->getAs<clang::CFGImplicitDtor>()) {
        const clang::CXXDestructorDecl *DestrD = ImplD->getDestructorDecl(Ctxt);
        Call = DestrD;
      }

      if (Call) {
        std::string CName = getMangledName(*Call);
        Builder.AddCall(FName.data(), CName);
      }
    }
  }
}

void ExtendCallGraph::GenerateOhmuIR(const std::string &FName,
                                     const clang::Decl *Fun,
                                     clang::AnalysisDeclContext &AC) {
  clang::tilcpp::ClangCFGWalker Walker;
  if (!Walker.init(AC))
    return;

  ohmu::MemRegion Region;
  ohmu::MemRegionRef Arena(&Region);
  clang::tilcpp::ClangTranslator SxBuilder(Arena);
  SxBuilder.setSSAMode(true);
  Walker.walk(SxBuilder);

  ohmu::til::BytecodeStringWriter WriteStream;
  ohmu::til::BytecodeWriter Writer(&WriteStream);

  Writer.traverseAll(SxBuilder.topLevelSlot());
  WriteStream.flush();
  Builder.SetOhmuIR(FName, WriteStream.str());
}

} // end namespace

namespace ohmu {
namespace lsa {

void DefaultCallGraphBuilder::AddCall(const std::string &From,
                                      const std::string &To) {
  CallGraphNode *FromNode = GetNodeByName(From);
  FromNode->AddCall(To);
}

void DefaultCallGraphBuilder::SetOhmuIR(const std::string &Func,
                                        const std::string &IR) {
  GetNodeByName(Func)->SetIR(IR);
}

DefaultCallGraphBuilder::CallGraphNode *
DefaultCallGraphBuilder::GetNodeByName(const std::string &Func) {
  auto It = Graph.find(Func);
  if (It != Graph.end())
    return It->second.get();
  CallGraphNode *Node = new CallGraphNode();
  Graph[Func] = std::unique_ptr<CallGraphNode>(Node);
  return Node;
}

void DefaultCallGraphBuilder::Print(std::ostream &Out) {
  for (const auto &p : Graph) {
    Out << p.first << "\n";
    p.second->Print(Out);
  }
}

void DefaultCallGraphBuilder::CallGraphNode::Print(std::ostream &Out) {
  for (std::string Called : OutgoingCalls)
    Out << "--> " << Called << "\n";

  ohmu::MemRegion Region;
  ohmu::MemRegionRef Arena(&Region);
  ohmu::til::CFGBuilder Builder(Arena);
  ohmu::til::InMemoryReader ReadStream(OhmuIR.data(), OhmuIR.length(), Arena);
  ohmu::til::BytecodeReader Reader(Builder, &ReadStream);
  auto *Expr = Reader.read();

  Out << "IR: ";
  ohmu::til::TILDebugPrinter::print(Expr, Out);
  Out << "\n";
}

void CallGraphBuilderTool::RegisterMatchers(
    CallGraphBuilder &Builder, clang::ast_matchers::MatchFinder *Finder) {

  ExtendCallGraph *Extender = new ExtendCallGraph(Builder);
  Finder->addMatcher(clang::ast_matchers::functionDecl(
                         clang::ast_matchers::decl().bind("decl")),
                     Extender);
  match_callbacks_.emplace_back(Extender);
}

} // namespace lsa
} // namespace ohmu
