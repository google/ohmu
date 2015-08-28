//===- EscapeAnalysis.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Example distributed graph computation, computing the escape information of
// parameters. This is a rather dumb analysis, and can be improved by:
// - including the lifetime of objects. I.e. assigning a longer-lived object to
//   a shorter-lived location should not constitute escaping.
// - not only analyze whether parameters escape, but also whether locally
//   created objects escape.
// - include returning a reference to a locally created object as a way of
//   escaping.
//
// The analysis first does an intra-procedural escape analysis on each vertex.
// During this analysis it records where parameters are passed to other
// functions. Subsequently vertices inform callers about their escape behavior.
// Callers update their escape behavior and in turn inform their callers if
// needed. This continues until the escape information is stabilized.
//===----------------------------------------------------------------------===//

#ifndef OHMU_LSA_EXAMPLES_ESCAPEANALYSIS_H
#define OHMU_LSA_EXAMPLES_ESCAPEANALYSIS_H

#include <array>
#include <unordered_set>

#include "clang/Analysis/Til/TILTraverse.h"
#include "lsa/GraphComputation.h"

namespace ohmu {
namespace lsa {

/// Represents information of a single argument to a call.
struct ArgumentInfo {
public:
  string FunctionName;    // Function to which the argument is provided.
  unsigned ArgumentPos;   // As which parameter the argument is passed.
  unsigned InstructionID; // Location of the function call.
};

/// A collection of argument information.
typedef std::vector<ArgumentInfo> ArgumentInfoArray;

/// The information stored at each vertex. Only the EscapeLocations vector is
/// serialized, all other information is recomputed if a vertex is restarted.
struct EscapeData {
public:
  EscapeData() : Initialized(false), ParameterCount(0) {}

  /// Whether the escape data has been initialized after the most recent vertex
  /// restart.
  bool Initialized;

  /// Number of parameters at this vertex.
  unsigned ParameterCount;

  /// Mapping from de-bruin index to where this parameter is used as an
  /// argument.
  std::vector<ArgumentInfoArray> ParameterAsArgument;

  /// Whether a parameter is a reference or pointer. I.e. whether a value can
  /// escape via that parameter.
  std::vector<bool> IsReference;

  /// Whether a parameter escapes.
  std::vector<bool> Escapes;

  /// Log the id's of the instructions where each parameter escapes by
  /// assignment.
  std::vector<std::unordered_set<unsigned>> EscapeLocations;
};

class EscapeAnalysis;

template <> struct GraphTraits<EscapeAnalysis> {
  typedef EscapeData VertexValueType;
  typedef std::vector<bool> MessageValueType;
};

/// Simple traversal that marks a parameter as escaping whenever it is assigned,
/// or passed to a function as an argument that is marked as escaping.
class EscapeTraversal : public ohmu::til::Traversal<EscapeTraversal>,
                        public ohmu::til::DefaultScopeHandler,
                        public ohmu::til::DefaultReducer {
public:
  typedef ohmu::til::Traversal<EscapeTraversal> Super;

  EscapeTraversal(unsigned NPara, std::vector<ArgumentInfoArray> *Uses,
                  std::vector<bool> *E)
      : NParameters(NPara), ParameterAsArgument(Uses), Escaped(E) {
    InstructionEscapes.resize(NParameters + 1);
  }

  void traverseSCFG(ohmu::til::SCFG *E) {
    WeakExpressionCache.resize(E->numInstructions());
    Super::traverseSCFG(E);
  }

  /// Check what parameters are on the right-hand side and mark them as
  /// escaping.
  void reduceStore(ohmu::til::Store *E);

  /// If a parameter is passed as an argument to a known function, register this
  /// forwarding of the parameter. If the function is unknown, mark the
  /// parameter as escaping.
  void reduceCall(ohmu::til::Call *E);

  /// Any parameter that is returned we mark as escaping. An improvement would
  /// be to track the returned parameters separately, so that this information
  /// can then be propagated by the caller.
  void reduceReturn(ohmu::til::Return *E);

private:
  /// From a sub-expression (i.e. right-hand side of a store, or argument to a
  /// call), return what parameter escapes, if any. Due to phi nodes, there may
  /// multiple parameters escaping.
  std::vector<unsigned> escapedParameter(ohmu::til::SExpr *E);

private:
  /// Number of parameters.
  unsigned NParameters;

  /// Per parameter, log at which instructions it escapes for better feedback.
  std::vector<std::unordered_set<unsigned>> InstructionEscapes;

  /// Per weak sub-expression, cache which parameters escape.
  std::vector<std::unique_ptr<std::vector<bool>>> WeakExpressionCache;

  /// Per parameter, the collection of locations where this parameter is passed
  /// as argument to another function.
  std::vector<ArgumentInfoArray> *ParameterAsArgument;

  /// Per parameter, whether it escapes in this function.
  std::vector<bool> *Escaped;
};

/// Distributed graph computation performing a simple escape analysis on
/// function parameters.
class EscapeAnalysis : public GraphComputation<EscapeAnalysis> {
public:
  /// First perform an escape analysis on the function at each vertex. Next
  /// keep forwarding escape information until no additional escape information
  /// is obtained.
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override;

  /// Simply produces a binary sequence indicating whether the n-th parameter
  /// escapes.
  string output(const GraphVertex *Vertex) const override;

private:
  /// Vertices might get restarted by the computation framework. Rather than
  /// serializing all information we recompute the escape information. The
  /// only information that might have been deserialized are the escape
  /// locations (possibly inferred from messages we received earlier in the
  /// computation), which we use to update the escape information.
  void initialize(GraphVertex *Vertex);

  /// Cycle through all parameters in this function. Marks which parameters are
  /// references/pointers and returns the number of parameters to this function.
  /// Assumes that the ohmu IR is generated using BuildCallGraph.h.
  unsigned processParameters(GraphVertex *Vertex);

  /// Updates whether the variable at position 'index' defined by this
  /// expression is a reference/pointer.
  void updateIsReference(GraphVertex *Vertex, ohmu::til::SExpr *E,
                         unsigned index);

  /// Runs an EscapeTraversal, updating the vertex's escape information.
  void escapeAnalysis(GraphVertex *Vertex);

  /// Given that for 'Function' the parameter at position 'ArgIndex' escapes,
  /// update the escape information at the current vertex (using the information
  /// logged in ParameterUses).
  /// Returns true if this changes the escape status of a parameter.
  bool updateEscapeData(GraphVertex *Vertex, const string &Function,
                        unsigned ArgIndex);
};

} // namespace lsa
} // namespace ohmu

/// Serialization for Google's Pregel framework.
template <> class StringCoderCustom<ohmu::lsa::EscapeData> {
public:
  static void Encode(const ohmu::lsa::EscapeData &value, string *result) {
    result->clear();
    ohmu::lsa::writeUInt64ToString(value.EscapeLocations.size(), result);
    for (const auto &ParameterEscapes : value.EscapeLocations) {
      ohmu::lsa::writeUInt64ToString(ParameterEscapes.size(), result);
      for (unsigned location : ParameterEscapes)
        ohmu::lsa::writeUInt64ToString(location, result);
    }
  }

  static bool Decode(const string &str, ohmu::lsa::EscapeData *result) {
    int index = 0;
    uint64_t nParameters = ohmu::lsa::readUInt64FromString(str, index);
    result->EscapeLocations.resize(nParameters + 1);
    for (unsigned p = 0; p < nParameters; p++) {
      uint64_t nLocations = ohmu::lsa::readUInt64FromString(str, index);
      for (unsigned i = 0; i < nLocations; i++) {
        result->EscapeLocations[p + 1].emplace(
            ohmu::lsa::readUInt64FromString(str, index));
      }
    }
    return true;
  }
};

#endif // OHMU_LSA_EXAMPLES_ESCAPEANALYSIS_H
