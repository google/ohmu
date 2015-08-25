//===- BuildCallGraph.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Construction of a call graph of C++ functions, paired with their Ohmu IR
// translated bodies.
//===----------------------------------------------------------------------===//

#ifndef OHMU_LSA_BUILDCALLGRAPH_H
#define OHMU_LSA_BUILDCALLGRAPH_H

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ohmu {
namespace lsa {

/// Interface for actually constructing the call graph from the discovered calls
/// and produced Ohmu IR. In this graph functions are identified by their C++
/// mangled name.
class CallGraphBuilder {
public:
  virtual ~CallGraphBuilder() {}

  /// Request to store the information that there is a path in function From on
  /// which function To is called.
  virtual void AddCall(const std::string &From, const std::string &To) = 0;

  /// Request to store the generated ohmu IR representation of the function
  /// identified by Func.
  virtual void SetOhmuIR(const std::string &Func, const std::string &IR) = 0;
};

/// The standard implementation of GraphConstructor stores the call graph as a
/// mapping from function identifier to CGNode.
class DefaultCallGraphBuilder : public CallGraphBuilder {
public:
  void AddCall(const std::string &From, const std::string &To) override;
  void SetOhmuIR(const std::string &Func, const std::string &IR) override;

  void Print(std::ostream &Out);

  class CallGraphNode {
  public:
    void AddCall(const std::string &To) { OutgoingCalls.insert(To); }
    void SetIR(const std::string &IR) { OhmuIR = IR; }
    void Print(std::ostream &Out);

    const std::unordered_set<std::string> *GetCalls() const {
      return &OutgoingCalls;
    }
    const std::string &GetIR() const { return OhmuIR; }

  private:
    std::unordered_set<std::string> OutgoingCalls;
    std::string OhmuIR;
  };

  const std::unordered_map<std::string, std::unique_ptr<CallGraphNode>> &
  GetGraph() {
    return Graph;
  }

private:
  /// Returns the CGNode currently constructed for the function identified by
  /// Func. Creates a new node if none is associated with this function yet.
  CallGraphNode *GetNodeByName(const std::string &Func);

  std::unordered_map<std::string, std::unique_ptr<CallGraphNode>>
      Graph; // Mapping function names to their nodes.
};

/// Tool to be used for creating call graphs with Ohmu IR for each function.
class CallGraphBuilderTool {
public:
  /// Create the required AST matchers and register them with 'Finder'.
  /// Matches all function declarations.
  void RegisterMatchers(CallGraphBuilder &Builder,
                        clang::ast_matchers::MatchFinder *Finder);

private:
  /// This tool creates and owns its MatchCallbacks.
  std::vector<std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback>>
      match_callbacks_;
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_BUILDCALLGRAPH_H
