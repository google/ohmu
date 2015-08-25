//===- SCCComputation.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Example distributed graph computation, computing the strongly connected
// components (SCC) in a graph. This is an implementation of the unoptimized SCC
// algorithm described by Yan et al. in "Pregel Algorithms for Graph
// Connectivity Problems with Performance Guarantees" from VLDB 2014:
//
//    http://www.vldb.org/pvldb/vol7/p1821-yan.pdf
//
// The algorithm consists of three phases which are repeated until all SCCs are
// found:
//  1) forward-min: vertices keep communicating the minimal vertex-id they have
//       encountered over their forward edges until no lower id is received.
//  2) backward-min: vertices keep communicating the minimal vertex-id they have
//       encountered over their backward edges until no lower id is received.
//  3) decompose: edges between vertices whose pairs (forwardMin, backwardMin),
//       ie their partitions, are not equal are removed
// Vertices are in an SCC when forwardMin = backwardMin.
// The SCCs are identified by the pair (forwardMin, backwardMin).
//===----------------------------------------------------------------------===//

#ifndef OHMU_LSA_EXAMPLES_SCCCOMPUTATION_H
#define OHMU_LSA_EXAMPLES_SCCCOMPUTATION_H

#include "lsa/GraphComputation.h"

namespace ohmu {
namespace lsa {

/// The type of the value at a vertex during SCC computation.
class SCCNode {
public:
  SCCNode();

  /// The minimal ID encountered sending over the outgoing calls.
  string ForwardMin;

  /// The minimal ID encountered sending over the incoming calls.
  string BackwardMin;
};

class SCCComputation;

template <> struct GraphTraits<SCCComputation> {
  typedef SCCNode VertexValueType;
  typedef string MessageValueType;
};

class SCCComputation : public GraphComputation<SCCComputation> {
public:
  void computePhase(GraphVertex *Vertex, const string &Phase,
                    MessageList Messages) override;

  string transition(const string &Phase) override;

  string output(const GraphVertex *Vertex) const override {
    return partitionID(Vertex);
  }

private:
  /// Returns true if the SCC of this vertex is known.
  bool inSCC(GraphVertex *Vertex);

  /// The computation phases.
  void forwardMin(GraphVertex *Vertex, MessageList Messages);
  void backwardMin(GraphVertex *Vertex, MessageList Messages);
  void decomposeGraph(GraphVertex *Vertex, MessageList Messages);

  /// Send a message with this vertex' current minimal value. The argument
  /// indicates whether we are in the forward or backward phase.
  void sendUpdateMessage(GraphVertex *Vertex, bool Forward);

  /// Returns this vertex' partition id. This is the concatenation of the
  /// ForwardMin and BackwardMin value.
  string partitionID(const GraphVertex *Vertex) const;
};

} // namespace ohmu
} // namespace lsa

/// Serialization for Google's Pregel framework.
template <> class StringCoderCustom<ohmu::lsa::SCCNode> {
public:
  static void Encode(const ohmu::lsa::SCCNode &value, string *result) {
    result->clear();
    writeUInt64_Vbr(value.ForwardMin.size(), result);
    result->append(value.ForwardMin);
    writeUInt64_Vbr(value.BackwardMin.size(), result);
    result->append(value.BackwardMin);
  }

  static bool Decode(const string &str, ohmu::lsa::SCCNode *result) {
    int index = 0;
    uint64_t length = readUInt64_Vbr(str, index);
    if (str.length() < index + length)
      return false;
    result->ForwardMin = str.substr(index, length);
    index += length;
    length = readUInt64_Vbr(str, index);
    if (str.length() < index + length)
      return false;
    result->BackwardMin = str.substr(index, length);
    return true;
  }

private:
  // Taken from til/Bytecode.cpp:
  static void writeUInt64_Vbr(uint64_t V, string *result) {
    if (V == 0) {
      result->push_back('\0');
      return;
    }
    while (V > 0) {
      uint64_t V2 = V >> 7;
      uint8_t Hibit = (V2 == 0) ? 0 : 0x80;
      // Write lower 7 bits.  The 8th bit is high if there's more to write.
      result->push_back(static_cast<char>((V & 0x7Fu) | Hibit));
      V = V2;
    }
  }

  static uint64_t readUInt64_Vbr(const string &str, int &index) {
    uint64_t V = 0;
    for (unsigned B = 0; B < 64; B += 7) {
      uint64_t Byt = str[index++];
      V = V | ((Byt & 0x7Fu) << B);
      if ((Byt & 0x80) == 0)
        break;
    }
    return V;
  }
};

#endif // OHMU_LSA_EXAMPLES_SCCCOMPUTATION_H
