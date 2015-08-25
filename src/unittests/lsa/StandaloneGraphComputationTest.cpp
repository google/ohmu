#include <string>

#include "gtest/gtest.h"
#include "lsa/StandaloneGraphComputation.h"

// Template specializations need to be performed in the same namespace due to
// -fpermissive flag.
class SinglePhaseComputation;
class TwoPhaseComputation;
class IteratedPhaseComputation;

namespace ohmu {
namespace lsa {

template <> struct GraphTraits<SinglePhaseComputation> {
  typedef int VertexValueType;
  typedef int MessageValueType;
};

template <> struct GraphTraits<TwoPhaseComputation> {
  typedef int VertexValueType;
  typedef int MessageValueType;
};

template <> struct GraphTraits<IteratedPhaseComputation> {
  typedef int VertexValueType;
  typedef int MessageValueType;
};

} // namespace lsa
} // namespace ohmu

/// Simple computation not using phases.
class SinglePhaseComputation
    : public ohmu::lsa::GraphComputation<SinglePhaseComputation> {
public:
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override {
    bool Updated = false;

    for (const Message &In : Messages) {
      if (In.value() > Vertex->value()) {
        *Vertex->mutableValue() = In.value();
        Updated = true;
      }
    }

    // Always send value in the first step.
    if (stepCount() == 0) {
      Updated = true;
    }

    if (Updated) {
      for (const string &Out : Vertex->outgoingCalls()) {
        Vertex->sendMessage(Out, Vertex->value());
      }
    } else {
      Vertex->voteToHalt();
    }
  }

  string output(const GraphVertex *Vertex) const override {
    return std::to_string(Vertex->value());
  }
};

/// Add vertices with values, check that generated graph contains expected
/// vetices and values.
TEST(StandaloneGraphComputation, BuildGraphVertices) {
  ohmu::lsa::StandaloneGraphBuilder<SinglePhaseComputation> Builder;
  string aId = "a", bId = "b", cId = "c";
  int aValue = 10, bValue = 5, cValue = 30;

  Builder.addVertex(aId, "", aValue);
  Builder.addVertex(bId, "", bValue);
  Builder.addVertex(cId, "", cValue);

  ASSERT_EQ(3, Builder.getVertices().size());

  for (const auto &Vertex : Builder.getVertices()) {
    if (Vertex.id() == aId)
      EXPECT_EQ(aValue, Vertex.value());
    if (Vertex.id() == bId)
      EXPECT_EQ(bValue, Vertex.value());
    if (Vertex.id() == cId)
      EXPECT_EQ(cValue, Vertex.value());
  }
}

/// Add calls, check that generated graph contains the expected calls.
TEST(StandaloneGraphComputation, BuildGraphCalls) {
  ohmu::lsa::StandaloneGraphBuilder<SinglePhaseComputation> Builder;
  string aId = "a", bId = "b", cId = "c";

  Builder.addCall(aId, bId);
  Builder.addCall(bId, aId);
  Builder.addCall(bId, cId);
  Builder.addCall(cId, aId);

  ASSERT_EQ(3, Builder.getVertices().size());

  for (const auto &Vertex : Builder.getVertices()) {
    if (Vertex.id() == aId) {
      const std::unordered_set<string> &OutCalls = Vertex.outgoingCalls();
      ASSERT_EQ(1, OutCalls.size());
      EXPECT_NE(OutCalls.end(), OutCalls.find(bId));
      const std::unordered_set<string> &InCalls = Vertex.incomingCalls();
      ASSERT_EQ(2, InCalls.size());
      EXPECT_NE(InCalls.end(), InCalls.find(bId));
      EXPECT_NE(InCalls.end(), InCalls.find(cId));
    }
    if (Vertex.id() == bId) {
      const std::unordered_set<string> &OutCalls = Vertex.outgoingCalls();
      ASSERT_EQ(2, OutCalls.size());
      EXPECT_NE(OutCalls.end(), OutCalls.find(aId));
      EXPECT_NE(OutCalls.end(), OutCalls.find(cId));
      const std::unordered_set<string> &InCalls = Vertex.incomingCalls();
      ASSERT_EQ(1, InCalls.size());
      EXPECT_NE(InCalls.end(), InCalls.find(aId));
    }
    if (Vertex.id() == cId) {
      const std::unordered_set<string> &OutCalls = Vertex.outgoingCalls();
      ASSERT_EQ(1, OutCalls.size());
      EXPECT_NE(OutCalls.end(), OutCalls.find(aId));
      const std::unordered_set<string> &InCalls = Vertex.incomingCalls();
      ASSERT_EQ(1, InCalls.size());
      EXPECT_NE(InCalls.end(), InCalls.find(bId));
    }
  }
}

/// Run a simple computation that has only one phase.
TEST(StandaloneGraphComputation, GraphComputationSinglePhase) {
  ohmu::lsa::StandaloneGraphBuilder<SinglePhaseComputation> Builder;
  string aId = "a", bId = "b", cId = "c";
  int aValue = 10, bValue = 5, cValue = 30;

  Builder.addVertex(aId, "", aValue);
  Builder.addVertex(bId, "", bValue);
  Builder.addVertex(cId, "", cValue);
  Builder.addCall(aId, bId);
  Builder.addCall(bId, cId);
  Builder.addCall(cId, aId);

  Builder.run(new ohmu::lsa::GraphComputationFactory<SinglePhaseComputation>());

  // All vertices should now hold the highest value.
  for (const auto &Vertex : Builder.getVertices()) {
    EXPECT_EQ(cValue, Vertex.value()) << "Vertex " << Vertex.id();
  }
}

/// Simple computation using one cycle of phases.
/// Assumes each vertex has at least one outgoing call and one incoming call.
/// START: forward own value, store first received value + 1
/// NEXT:  forward new value, store first received value.
class TwoPhaseComputation
    : public ohmu::lsa::GraphComputation<TwoPhaseComputation> {
public:
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override {
    if (stepCount() == 0) {
      Vertex->sendMessage(*Vertex->outgoingCalls().begin(), Vertex->value());
    } else {
      if (Phase.compare("START") == 0) {
        *Vertex->mutableValue() = Messages.begin()->value() + 1;
      } else if (Phase.compare("NEXT") == 0) {
        *Vertex->mutableValue() = Messages.begin()->value();
      }
    }

    Vertex->voteToHalt();
  }

  string transition(const string &Phase) override {
    return Phase.compare("START") == 0 ? "NEXT" : "HALT";
  }

  string output(const GraphVertex *Vertex) const override {
    return std::to_string(Vertex->value());
  }
};

/// Run a simple computation that has multiple phases, one iteration.
TEST(StandaloneGraphComputation, GraphComputationTwoPhase) {
  ohmu::lsa::StandaloneGraphBuilder<TwoPhaseComputation> Builder;
  string aId = "a", bId = "b", cId = "c";
  int aValue = 10, bValue = 5, cValue = 30;

  Builder.addVertex(aId, "", aValue);
  Builder.addVertex(bId, "", bValue);
  Builder.addVertex(cId, "", cValue);
  Builder.addCall(aId, bId);
  Builder.addCall(bId, cId);
  Builder.addCall(cId, aId);

  Builder.run(new ohmu::lsa::GraphComputationFactory<TwoPhaseComputation>());

  // All vertices should now hold the original value of the node two calls back,
  // incremented with one.
  for (const auto &Vertex : Builder.getVertices()) {
    if (Vertex.id() == aId)
      EXPECT_EQ(bValue + 1, Vertex.value());
    if (Vertex.id() == bId)
      EXPECT_EQ(cValue + 1, Vertex.value());
    if (Vertex.id() == cId)
      EXPECT_EQ(aValue + 1, Vertex.value());
  }
}

/// Simple computation cycling through multiple iterations.
/// Keeps running two phases as the TwoPhaseComputation, but only increases the
/// value
/// if it is below 10. Once all values are 10 or higher, the cycling stops.
class IteratedPhaseComputation
    : public ohmu::lsa::GraphComputation<IteratedPhaseComputation> {
public:
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override {

    if (stepCount() == 0) {
      Vertex->sendMessage(*Vertex->outgoingCalls().begin(), Vertex->value());
    } else {
      if (Phase.compare("START") == 0) {
        if (Messages.begin()->value() < 10) {
          *Vertex->mutableValue() = Messages.begin()->value() + 1;
        } else {
          *Vertex->mutableValue() = Messages.begin()->value();
        }
      } else if (Phase.compare("NEXT") == 0) {
        *Vertex->mutableValue() = Messages.begin()->value();
      }
    }

    Vertex->voteToHalt();

    // Keep iterating phases until all vertices have a value of at least 10.
    if (Vertex->value() < 10) {
      Vertex->voteToReiterate();
    }
  }

  string transition(const string &Phase) override {
    if (!shouldReiterate())
      return "HALT";
    if (Phase.compare("START") == 0)
      return "NEXT";
    if (Phase.compare("NEXT") == 0)
      return "START";
    return "HALT";
  }

  string output(const GraphVertex *Vertex) const override {
    return std::to_string(Vertex->value());
  }
};

/// Run a simple computation that has multiple phases and multiple iterations.
TEST(StandaloneGraphComputation, GraphComputationTwoPhaseIterate) {
  ohmu::lsa::StandaloneGraphBuilder<IteratedPhaseComputation> Builder;
  string aId = "a", bId = "b", cId = "c";
  int aValue = 9, bValue = 6, cValue = 30;

  Builder.addVertex(aId, "", aValue);
  Builder.addVertex(bId, "", bValue);
  Builder.addVertex(cId, "", cValue);
  Builder.addCall(aId, bId);
  Builder.addCall(bId, cId);
  Builder.addCall(cId, aId);

  Builder.run(
      new ohmu::lsa::GraphComputationFactory<IteratedPhaseComputation>());

  // The value at vertex b takes 4 iterations to reach value 10, meaning that
  // all values are shifted (4*2 % 3) = 2 steps
  for (const auto &Vertex : Builder.getVertices()) {
    if (Vertex.id() == aId)
      EXPECT_EQ(bValue + 4, Vertex.value());
    if (Vertex.id() == bId)
      EXPECT_EQ(cValue, Vertex.value());
    if (Vertex.id() == cId)
      EXPECT_EQ(aValue + 1, Vertex.value());
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
