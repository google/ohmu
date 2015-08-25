#include <unordered_map>

#include "gtest/gtest.h"
#include "lsa/examples/SCCComputation.h"

namespace {
/// Return the partition identifier as it is used in the SCC computation.
string partition(string id) { return id + ":" + id; }

/// Actually runs the test. Creates the graph with specified vertices and edges.
/// Runs the SCC computation and checks whether each vertex ends up in the
/// expected SCC (partition).
void TestSCC(const std::vector<string> &vertices,
             const std::vector<std::pair<string, string>> &edges,
             const std::unordered_map<string, string> &expected) {
  ohmu::lsa::StandaloneGraphBuilder<ohmu::lsa::SCCComputation> Builder;

  for (const string &vertex : vertices) {
    ohmu::lsa::SCCNode node;
    Builder.addVertex(vertex, "", node);
  }

  for (const auto &edge : edges) {
    Builder.addEdge(edge.first, edge.second, true);
    Builder.addEdge(edge.second, edge.first, false);
  }
  ohmu::lsa::GraphComputationFactory<ohmu::lsa::SCCComputation> Factory;
  Builder.run(&Factory);

  std::unique_ptr<ohmu::lsa::GraphComputation<ohmu::lsa::SCCComputation>>
      Computation(Factory.createComputation());
  for (const auto &Vertex : Builder.getVertices()) {
    string expected_partition = expected.find(Vertex.id())->second;
    string actual_partition = Computation->output(&Vertex);
    EXPECT_EQ(expected_partition, actual_partition)
        << "When checking SCC of vertex " << Vertex.id() << ".\n";
  }
}

} // end namespace

TEST(SCCComputation, SingletonSCC) {
  string aId = "a", bId = "b", cId = "c";

  // Generated graph:
  //
  //  a         b         c
  //
  // SCC #1: {a}
  // SCC #2: {b}
  // SCC #3: {c}

  std::vector<string> vertices = {aId, bId, cId};
  std::vector<std::pair<string, string>> edges = {};
  std::unordered_map<string, string> expected = {
      {aId, partition(aId)}, {bId, partition(bId)}, {cId, partition(cId)}};

  TestSCC(vertices, edges, expected);
}

TEST(SCCComputation, OneSCC) {
  string aId = "a", bId = "b", cId = "c";

  // Generated graph:
  //
  //  a  ---->  b  ---->  c
  //  ^                   |
  //  \-------------------/
  //
  // SCC #1: {a, b, c}

  std::vector<string> vertices = {aId, bId, cId};
  std::vector<std::pair<string, string>> edges = {
      {aId, bId}, {bId, cId}, {cId, aId}};
  std::unordered_map<string, string> expected = {
      {aId, partition(aId)}, {bId, partition(aId)}, {cId, partition(aId)}};

  TestSCC(vertices, edges, expected);
}

TEST(SCCComputation, TwoSCC) {
  string aId = "a", bId = "b", cId = "c", dId = "d", eId = "e", fId = "f",
         gId = "g";

  // Generated graph:
  //
  //  a  ---->  b  ---->  c  ---->  d  ---->  e
  //  ^         ^         |         ^         |
  //  |         |         |         |         |
  //  |         |         |         v         |
  //  \-------  f  <------/         g  <------/
  //
  // SCC #1: {a, b, c, f}
  // SCC #2: {d, e, g}

  std::vector<string> vertices = {aId, bId, cId, dId, eId, fId, gId};
  std::vector<std::pair<string, string>> edges = {
      {aId, bId}, {bId, cId}, {cId, fId}, {cId, dId}, {dId, eId},
      {dId, gId}, {eId, gId}, {fId, bId}, {fId, aId}, {gId, dId}};
  std::unordered_map<string, string> expected = {
      {aId, partition(aId)}, {bId, partition(aId)}, {cId, partition(aId)},
      {fId, partition(aId)}, {dId, partition(dId)}, {eId, partition(dId)},
      {gId, partition(dId)}};

  TestSCC(vertices, edges, expected);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
