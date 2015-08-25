//===- SCCComputation.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Simple computation that checks whether a function, or any of the functions it
// calls, modifies a global variable. It serves as an example of how to run an
// Ohmu analysis, and is by no means perfect :)
//
// The computation only finds direct changes to global variables, not changes
// via e.g. aliased values or passed-by-reference. It also has false positive,
// e.g. when assigning to a local reference.
//===----------------------------------------------------------------------===//

#ifndef OHMU_LSA_EXAMPLES_EXAMPLEOHMUOMPUTATION_H
#define OHMU_LSA_EXAMPLES_EXAMPLEOHMUOMPUTATION_H

// NOTE: Whatever is included should not include ohmu's version of MemRegion.h,
// since in a standalone computation this code is combined with the call graph
// builder, which uses the version of MemRegion.h residing in Clang, which is
// different. The solution is to let the standalone version also generate
// intermediate results on disks, allowing us to create separate binaries for
// call graph generation and analysis.

#include "clang/Analysis/Til/TILTraverse.h"
#include "lsa/GraphComputation.h"

namespace ohmu {
namespace lsa {

class OhmuComputation;

template <> struct GraphTraits<OhmuComputation> {
  // True if we think this function changes a global variable.
  typedef bool VertexValueType;
  typedef bool MessageValueType;
};

/// Simple traversal that looks for any store operation of the kind "a.b := c"
/// where "a" is not part of a record. If such a store operation exists, we
/// conclude that "b" is a global variable and mark this function as modifying
/// global variables.
class FindGlobalModification
    : public ohmu::til::Traversal<FindGlobalModification>,
      public ohmu::til::DefaultScopeHandler,
      public ohmu::til::DefaultReducer {
public:
  typedef ohmu::til::Traversal<FindGlobalModification> SuperTv;

  FindGlobalModification() : MadeModification(false) {}

  void reduceStore(ohmu::til::Store *E) {
    if (auto *Dest = ohmu::dyn_cast<ohmu::til::Project>(E->destination())) {
      if (!Dest->record()) {
        MadeModification = true;
      }
    }
  }

  /// Shortcut traversal if we already know that this function makes global
  /// modifications.
  template <class T> void traverse(T *E, ohmu::til::TraversalKind K) {
    if (!MadeModification)
      SuperTv::traverse(E, K);
  }

  /// Returns true if the traversed function modifies a global variable.
  bool madeModification() { return MadeModification; }

protected:
  bool MadeModification;
};

/// Distributed graph computation that determines whether calling a function
/// changes a global variable. In the first step it computes the changes made
/// in this function body; then forwards this information to its callers.
class OhmuComputation : public GraphComputation<OhmuComputation> {
public:
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override {

    // First step, compute if this function modifies a global variable. If so,
    // update the state and inform callers.
    if (stepCount() == 0) {
      *Vertex->mutableValue() = modifiesGlobal(Vertex);

      if (Vertex->value())
        for (const string &Out : Vertex->outgoingCalls())
          Vertex->sendMessage(Out, true);

      // Second step; only care about incoming messages if so far we think this
      // function does not change global variables. If one of the functions we
      // call informs us that it changes a global variable, update the state
      // and inform callers of this function.
    } else if (!Vertex->value()) {
      for (const Message &In : Messages) {
        if (In.value()) {
          *Vertex->mutableValue() = true;
        }
      }
      if (Vertex->value())
        for (const string &Out : Vertex->outgoingCalls())
          Vertex->sendMessage(Out, true);
    }

    Vertex->voteToHalt();
  }

  string output(const GraphVertex *Vertex) const override {
    return Vertex->value() ? "yes" : "no";
  }

private:
  /// Run traversal to determine if this function changes a global variable.
  bool modifiesGlobal(GraphVertex *Vertex) {
    if (!Vertex->ohmuIR()) {
      std::cerr << "Could not read OhmuIR of " << Vertex->id() << ".\n";
      return false;
    }

    FindGlobalModification Finder;
    Finder.traverseAll(Vertex->ohmuIR());
    return Finder.madeModification();
  }
};

} // namespace ohmu
} // namespace lsa

#endif // OHMU_LSA_EXAMPLES_EXAMPLEOHMUOMPUTATION_H
