//===- BuildCallGraph.cpp --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/CFG.h"
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
  explicit ExtendCallGraph(ohmu::lsa::CallGraphBuilder &C) : Constructor(C) {}

  void
  run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  /// Returns the CFG of the body of the provided function declaration.
  std::unique_ptr<clang::CFG> BuildCFG(const clang::FunctionDecl &Fun);

  /// Traverses the CFG for calls to functions, constructors and destructors.
  void DiscoverCallGraph(const std::string &FName, clang::ASTContext &Ctxt,
                         clang::CFG *CFG);

  /// Generates the Ohmu IR of the CFG.
  std::string GenerateOhmuIR(clang::CFG *CFG);

private:
  ohmu::lsa::CallGraphBuilder &Constructor;
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
  std::unique_ptr<clang::CFG> CFG = BuildCFG(*Fun);

  Constructor.SetOhmuIR(FName, GenerateOhmuIR(CFG.get()));
  DiscoverCallGraph(FName, Fun->getASTContext(), CFG.get());
}

std::unique_ptr<clang::CFG>
ExtendCallGraph::BuildCFG(const clang::FunctionDecl &Fun) {
  clang::CFG::BuildOptions opt;
  opt.AddTemporaryDtors = true;
  opt.AddImplicitDtors = true;
  clang::ASTContext *Ctxt = &Fun.getASTContext();
  return clang::CFG::buildCFG(&Fun, Fun.getBody(), Ctxt, opt);
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
        Constructor.AddCall(FName.data(), CName);
      }
    }
  }
}

std::string ExtendCallGraph::GenerateOhmuIR(clang::CFG *cfg) {
  // TODO Generate Ohmu IR.
  return "add Ohmu IR here";
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
  Out << "IR: " << OhmuIR << "\n";
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
