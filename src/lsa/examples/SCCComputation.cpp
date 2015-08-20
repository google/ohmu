//===- SCCComputation.cpp --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "SCCComputation.h"

namespace {
/// Phase identifiers
const char kPhaseForward[] = "phase_forward";
const char kPhaseBackward[] = "phase_backward";
const char kPhaseDecompose[] = "phase_decompose";

/// Special value representing 'infinity' as vertex identity. Thus assuming that
/// this is not a value that can appear as a real identity.
const char kInfinity[] = "INF";
} // end namespace

namespace ohmu {
namespace lsa {

SCCNode::SCCNode() {
  ForwardMin = kInfinity;
  BackwardMin = kInfinity;
}

string SCCComputation::transition(const string &Phase) {
  if (!shouldReiterate())
    return "HALT";
  if (Phase == "START") {
    return kPhaseForward;
  } else if (Phase == kPhaseForward) {
    return kPhaseBackward;
  } else if (Phase == kPhaseBackward) {
    return kPhaseDecompose;
  } else if (Phase == kPhaseDecompose) {
    return kPhaseForward;
  }
  return "HALT";
}

void SCCComputation::computePhase(GraphVertex *Vertex, const string &Phase,
                                  MessageList Messages) {
  // As long as some vertex is not in a known SCC, we should keep cycling
  // through the phases.
  if (!inSCC(Vertex)) {
    Vertex->voteToReiterate();
    if (Phase == kPhaseForward) {
      forwardMin(Vertex, Messages);
    } else if (Phase == kPhaseBackward) {
      backwardMin(Vertex, Messages);
    } else if (Phase == kPhaseDecompose) {
      decomposeGraph(Vertex, Messages);
    }
  }

  // Always halt; only wake up on incoming messages.
  Vertex->voteToHalt();
}

/// First set the current ForwardMin to this vertex' id and forward it on all
/// outgoing edges. While the incoming messages contain an id lower than
/// ForwardMin, update it and forward the new lowest value.
void SCCComputation::forwardMin(GraphVertex *Vertex, MessageList Messages) {
  if (stepCount() == 0) {
    (*Vertex->mutableValue()).ForwardMin = Vertex->id();
    sendUpdateMessage(Vertex, true);
  } else {
    bool updated = false;
    for (const Message &Incoming : Messages) {
      if (Incoming.value().compare(Vertex->value().ForwardMin) < 0 ||
          Vertex->value().ForwardMin == kInfinity) {
        (*Vertex->mutableValue()).ForwardMin = Incoming.value();
        updated = true;
      }
    }
    // If we updated ForwardMin, inform our forward-neighbours.
    if (updated)
      sendUpdateMessage(Vertex, true);
  }
}

/// First set the current BackwardMin to this vertex' id if it received its
/// own id as ForwardMin, otherwise to infinite. While the incoming messages
/// contain an id lower than BackwardMin, update it and backward the new value.
void SCCComputation::backwardMin(GraphVertex *Vertex, MessageList Messages) {
  if (stepCount() == 0) {
    if (Vertex->id() != Vertex->value().ForwardMin) {
      (*Vertex->mutableValue()).BackwardMin = kInfinity;
    } else {
      (*Vertex->mutableValue()).BackwardMin = Vertex->id();
      sendUpdateMessage(Vertex, false);
    }
  } else {
    bool updated = false;
    for (const Message &Incoming : Messages) {
      if (Incoming.value().compare(Vertex->value().BackwardMin) < 0 ||
          Vertex->value().BackwardMin == kInfinity) {
        (*Vertex->mutableValue()).BackwardMin = Incoming.value();
        updated = true;
      }
    }
    // If we updated BackwardMin, inform our backward-neighbours.
    if (updated)
      sendUpdateMessage(Vertex, false);
  }
}

/// In step 0, send on all outgoing edges this vertex' partition id.
/// In SCCComputation 1, remove edges to vertices that sent a different
/// partition id.
void SCCComputation::decomposeGraph(GraphVertex *Vertex, MessageList Messages) {
  string partition = partitionID(Vertex);
  if (stepCount() == 0) {
    EdgeList Edges = Vertex->getOutEdges();
    for (const Edge &OutEdge : Edges) {
      if (OutEdge.value() == true) // Outgoing call
        Vertex->sendMessage(OutEdge.destination(), partition);
    }
  } else {
    for (const Message &Incoming : Messages) {
      if (Incoming.value() != partition) {
        removeEdges(Vertex->id(), Incoming.source());
        removeEdges(Incoming.source(), Vertex->id());
      }
    }
  }
}

bool SCCComputation::inSCC(GraphVertex *Vertex) {
  return Vertex->value().ForwardMin != kInfinity &&
         Vertex->value().ForwardMin == Vertex->value().BackwardMin;
}

string SCCComputation::partitionID(const GraphVertex *Vertex) const {
  std::stringstream Stream;
  Stream << Vertex->value().ForwardMin;
  Stream << ":";
  Stream << Vertex->value().BackwardMin;
  return Stream.str();
}

void SCCComputation::sendUpdateMessage(GraphVertex *Vertex, bool Forward) {
  EdgeList Edges = Vertex->getOutEdges();
  for (const Edge &OutEdge : Edges) {
    if (OutEdge.value() == Forward && Forward) {
      Vertex->sendMessage(OutEdge.destination(), Vertex->value().ForwardMin);
    } else if (OutEdge.value() == Forward && !Forward) {
      Vertex->sendMessage(OutEdge.destination(), Vertex->value().BackwardMin);
    }
  }
}

} // namespace ohmu
} // namespace lsa
