//===- StandaloneGraphComputation.h ----------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// Framework for running distributed graph computations locally. The computation
// runs in phases, each phase consisting of several steps. At each step the
// method "computePhase" is called for each vertex, providing the messages
// that vertex received in the previous step. In this computation step a vertex
// can vote to halt the computation, making the vertex inactive until it
// receives new messages. When all vertices have voted to halt, the "transition"
// function is called to determine the next phase. Special phases are "START",
// the first phase, and "HALT", which terminates the computation. Once the
// computation has terminated the "output" function is called once for every
// vertex.
//===----------------------------------------------------------------------===/

#ifndef OHMU_LSA_STANDALONEGRAPHCOMPUTATION_H
#define OHMU_LSA_STANDALONEGRAPHCOMPUTATION_H

#include <algorithm>
#include <map>
#include <memory>
#include <thread>

#include "clang/Analysis/Til/Bytecode.h"
#include "clang/Analysis/Til/CFGBuilder.h"

/// Allow for custom string type.
#ifndef HAS_GLOBAL_STRING
using std::string;
#endif

namespace ohmu {
namespace lsa {

/// A message send between two vertices.
template <class MessageValueType> class Message {
public:
  Message(const MessageValueType &V, const string &S) : Value(V), Source(S) {}

  const MessageValueType &value() const { return Value; }

  const string &source() const { return Source; }

private:
  MessageValueType Value;
  string Source;
};

/// A collection of messages.
template <class MessageValueType>
using MessageList = std::vector<Message<MessageValueType>>;

/// An outgoing edge.
template <class EdgeValueType> class Edge {
public:
  Edge(const EdgeValueType &V, const string &D) : Value(V), Destination(D) {}

  const EdgeValueType &value() const { return Value; }

  const string &destination() const { return Destination; }

private:
  EdgeValueType Value;
  string Destination;
};

/// A collection of edges.
template <class EdgeValueType>
using EdgeList = std::vector<Edge<EdgeValueType>>;

/// These traits describing the types of values residing on vertices, edges and
/// send as messages should be specialized by each computation.
template <class T> struct GraphTraits {
  typedef void VertexValueType; // Must provide a default constructor.
  typedef void EdgeValueType;
  typedef void MessageValueType;
};

/// Forward declaration of the tool that controls the computation.
template <class UserComputation> class StandaloneGraphTool;

/// Implementation of the GraphVertex API for standalone computations. All
/// methods only make local changes to enable easy multithreading.
template <class UserComputation> class GraphVertex {
public:
  typedef ohmu::lsa::GraphTraits<UserComputation> Traits;
  typedef typename Traits::VertexValueType VertexValueType;
  typedef typename Traits::MessageValueType MessageValueType;
  typedef typename Traits::EdgeValueType EdgeValueType;
  typedef ohmu::lsa::EdgeList<EdgeValueType> EdgeList;
  typedef ohmu::lsa::MessageList<MessageValueType> MessageList;

public:
  GraphVertex(const string &Id)
      : VertexId(Id), OhmuIR(nullptr), OhmuIRBuilt(false),
        Value(VertexValueType()), HaltVote(false), ReiterateVote(false) {}

public:
  /// The identity of this vertex.
  const string &id() const { return VertexId; }

  /// The ohmu IR of this function.
  ohmu::til::SExpr *ohmuIR() {
    if (!OhmuIRBuilt)
      buildOhmuIR();
    return OhmuIR;
  }

  /// Get a non-mutable reference to the value at this vertex.
  const VertexValueType &value() const { return Value; }

  /// Get a mutable pointer to the value at this vertex.
  VertexValueType *mutableValue() { return &Value; }

  /// Get the list of edges leaving this vertex.
  const EdgeList &getOutEdges() const { return OutEdges; }

  /// Send a message to the vertex with identity 'Destination'. The message is
  /// cached locally, relying on the StandaloneGraphTool to actually move the
  /// messages to the destinations after each step.
  void sendMessage(const string &Destination,
                   const MessageValueType &MessageValue) {
    getOutMessagesTo(Destination)
        .emplace_back(Message<MessageValueType>(MessageValue, VertexId));
  }

  /// Indicate that for this vertex the current phase is finished. This vertex
  /// becomes inactive for the remainder of this phase, unless it receives new
  /// messages.
  void voteToHalt() { HaltVote = true; }

  /// For algorithms that iterate through their phases multiple times, a vertex
  /// should call this function in each phase when it wants another iteration.
  /// If no vertex votes to reiterate, the function 'shouldReiterate' in the
  /// user computation returns false, which can be used to break the iteration
  /// if desired.
  void voteToReiterate() { ReiterateVote = true; }

private:
  /// Return all messages currently queued to be send to the vertex identified
  /// by 'Id'.
  MessageList &getOutMessagesTo(const string &Id) {
    auto Pair = OutMessagesCache.emplace(std::make_pair(Id, MessageList()));
    return Pair.first->second;
  }

  void buildOhmuIR() {
    ohmu::MemRegionRef Arena(&Region);
    ohmu::til::CFGBuilder Builder(Arena);

    ohmu::til::InMemoryReader ReadStream(OhmuIRRaw.data(), OhmuIRRaw.length(),
                                         Arena);
    ohmu::til::BytecodeReader Reader(Builder, &ReadStream);
    OhmuIR = Reader.read();
  }

private:
  string VertexId;
  string OhmuIRRaw;
  ohmu::til::SExpr *OhmuIR;
  ohmu::MemRegion Region; // Holding the IR.
  bool OhmuIRBuilt;
  VertexValueType Value;
  bool HaltVote;
  bool ReiterateVote;
  std::vector<Edge<EdgeValueType>> OutEdges;

  std::map<string, MessageList> OutMessagesCache;

private:
  /// To access internal representation without exposing an interface to the
  /// user code.
  friend ohmu::lsa::StandaloneGraphTool<UserComputation>;
};

/// This interface should be implemented by user computations to provide
/// 'computePhase', 'transition' and 'output'.
template <class UserComputation> class GraphComputation {
public:
  typedef ohmu::lsa::GraphTraits<UserComputation> Traits;
  typedef typename Traits::VertexValueType VertexValueType;
  typedef typename Traits::MessageValueType MessageValueType;
  typedef typename Traits::EdgeValueType EdgeValueType;

  typedef ohmu::lsa::StandaloneGraphTool<UserComputation> StandaloneGraphTool;
  typedef ohmu::lsa::GraphVertex<UserComputation> GraphVertex;
  typedef ohmu::lsa::Message<MessageValueType> Message;
  typedef ohmu::lsa::MessageList<MessageValueType> MessageList;
  typedef ohmu::lsa::Edge<EdgeValueType> Edge;
  typedef ohmu::lsa::EdgeList<EdgeValueType> EdgeList;

  virtual ~GraphComputation() {}

  // Methods to be overwritten by user computation.
public:
  /// This function should be implemented to perform the actual computation.
  virtual void computePhase(GraphVertex *Vertex, const string &Phase,
                            MessageList Messages) = 0;

  /// Can be called at the end of the computation to return the result of the
  /// computation at this vertex.
  virtual string output(const GraphVertex *Vertex) const = 0;

  /// Overwrite this function for multi-phase algorithms. The computation
  /// framework starts with the phase "START". To indicate that no more phases
  /// should be executed, return the phase "HALT";
  virtual string transition(const string &Phase) { return "HALT"; }

public:
  /// Get the current step number in this phase (starting at 0).
  int stepCount() const { return Tool->stepCount(); }

  /// Remove all edges from 'Source' to 'Destination'.
  void removeEdges(const string &Source, const string &Destination) {
    RemoveRequests.emplace_back(std::pair<string, string>(Source, Destination));
  }
  /// When running a iterating multi-phase algorithm, this function can be used
  /// in the 'transition' function to determine whether iteration should
  /// continue. Vertices can indicate that another iteration is required by
  /// calling 'voteToReiterate'.
  bool shouldReiterate() { return Tool->shouldReiterate(); }

private:
  // For easy multithreading a computation caches the remove requests, allowing
  // several computations to be ran in parallel.
  std::vector<std::pair<string, string>> RemoveRequests;

  // Set by friend class StandaloneGraphTool, points to computation controller.
  StandaloneGraphTool *Tool;

private:
  /// To access internal representation without exposing an interface to the
  /// user code.
  friend StandaloneGraphTool;
};

/// The factory enables us to use a separate computation instance per thread,
/// in that way allowing us to store a cache of removed edges (and in the
/// future possibly removed vertices, added edges etc) per thread, avoiding the
/// need for access to shared memory.
/// The standard implementation assumes that the user computation has a default
/// constructor.
template <class UserComputation> class GraphComputationFactory {
public:
  typedef ohmu::lsa::GraphComputation<UserComputation> GraphComputation;
  virtual GraphComputation *createComputation() {
    return new UserComputation();
  }
  virtual ~GraphComputationFactory() {}
};

/// Tool controlling the standalone computation. Its methods for constructing
/// the graph and running the algorithm are exposed via StandaloneGraphBuilder
/// (hiding the functions exposed to the user computation).
template <class UserComputation> class StandaloneGraphTool {
public:
  typedef ohmu::lsa::GraphTraits<UserComputation> Traits;
  typedef typename Traits::VertexValueType VertexValueType;
  typedef typename Traits::MessageValueType MessageValueType;
  typedef typename Traits::EdgeValueType EdgeValueType;
  typedef std::vector<Message<MessageValueType>> MessageList;

  typedef ohmu::lsa::GraphComputationFactory<UserComputation>
      GraphComputationFactory;
  typedef ohmu::lsa::GraphComputation<UserComputation> GraphComputation;
  typedef ohmu::lsa::GraphVertex<UserComputation> GraphVertex;

  StandaloneGraphTool() : StepCount(0), ReiterateResult(false), Phase("START") {
    // For now, we start as many threads as there are cores. In the future we
    // can probably provide the option to override this via a commandline flag.
    NCores = std::thread::hardware_concurrency();
    if (NCores == 0) // Value is not well defined or not computable.
      NCores = 1;
  }

  /// Methods exposed via StandaloneGraphBuilder.
public:
  /// Adds a vertex with the specified identity and value. If the vertex already
  /// exists, that vertex' value is updated instead of creating a new vertex.
  void addVertex(const string &Id, const string &IRRaw,
                 const VertexValueType Value) {
    GraphVertex &Vertex = getVertex(Id);
    *Vertex.mutableValue() = Value;
    Vertex.OhmuIRRaw = IRRaw;
  }

  /// Adds an edge between two vertices with the specified value. If a vertex
  /// does not exist, it is created using the default constructor for its value.
  void addEdge(const string &Source, const string &Destination,
               const EdgeValueType &Value) {
    getVertex(Destination); // To ensure vertex exists.
    getVertex(Source)
        .OutEdges.emplace_back(Edge<EdgeValueType>(Value, Destination));
  }

  /// Returns the current set of vertices.
  const std::vector<GraphVertex> &getVertices() { return Vertices; }

  /// Run the computation created by the specified factory.
  void run(GraphComputationFactory *Factory);

  /// Methods called by GraphComputation.
public:
  /// Get the current step number in this phase (starting at 0)
  int stepCount() const { return StepCount; }

  /// Returns whether any vertex requested further phase iterations.
  bool shouldReiterate() { return ReiterateResult; }

private:
  /// Returns the vertex with identity 'Id'. If no such vertex exists, one is
  /// created with the default value.
  GraphVertex &getVertex(const string &Id) {
    unsigned index =
        VertexMap.emplace(Id, Vertices.size())
            .first->second;
    if (index == Vertices.size())
      Vertices.emplace_back(GraphVertex(Id));
    return Vertices[index];
  }

  /// Returns true if all vertices have halted.
  bool phaseCompleted();

  /// Runs a step for all vertices.
  void runVerticesStep();

  /// Move messages from senders to receivers and apply requests for removing
  /// edges.
  void applyGraphChanges();

  /// Returns the messages that were sent to vertex 'Id' in the previous
  /// computation step.
  const MessageList &getMessagesTo(const string &Id) const {
    // Deliberate memory leak to create a method-default return value.
    static const MessageList NoMessages = *(new MessageList());
    auto El = Messages.find(Id);
    if (El == Messages.end()) {
      return NoMessages;
    }
    return El->second;
  }

  /// Move the outgoing messages cached by each vertex to the right destination.
  void sendMessages(const string &Destination, MessageList Incoming) {
    MessageList &DestinationMessages =
        Messages.emplace(Destination, MessageList()).first->second;
    std::move(Incoming.begin(), Incoming.end(),
              std::back_inserter(DestinationMessages));
  }

  /// Remove all edges between 'Source' and 'Destination'.
  void removeEdge(const string &Source, const string &Destination) {
    auto Element = VertexMap.find(Source);
    if (Element != VertexMap.end()) {
      unsigned index = Element->second;
      std::vector<Edge<EdgeValueType>> &OutEdges = Vertices[index].OutEdges;
      OutEdges.erase(
          std::remove_if(OutEdges.begin(), OutEdges.end(),
                         [&Destination](const Edge<EdgeValueType> &Out) {
                           return Out.destination().compare(Destination) == 0;
                         }),
          OutEdges.end());
    }
  }

private:
  int StepCount;
  bool ReiterateResult;
  string Phase;
  unsigned NCores;
  std::map<string, unsigned> VertexMap;
  std::vector<GraphVertex> Vertices;
  std::map<string, MessageList> Messages;

  /// 'NCores' computations to be run multithreaded, each caching the graph
  /// changes made in a computation step.
  std::vector<std::unique_ptr<GraphComputation>> UserComputations;
};

template <class C>
void StandaloneGraphTool<C>::run(GraphComputationFactory *Factory) {

  // Create separate computations for all threads, allowing for caching graph
  // changes per thread.
  UserComputations.clear();
  for (unsigned i = 0; i < NCores; i++) {
    std::unique_ptr<GraphComputation> Computation(Factory->createComputation());
    Computation->Tool = this;
    UserComputations.emplace_back(move(Computation));
  }

  while (Phase.compare("HALT") != 0) {

    // New phase, reset step counter and wake up all vertices.
    StepCount = 0;
    for (auto &Vertex : Vertices) {
      Vertex.HaltVote = false;
      Vertex.ReiterateVote = false;
    }

    while (!phaseCompleted()) {
      runVerticesStep();
      applyGraphChanges();
      ++StepCount;
    }

    Phase = UserComputations[0]->transition(Phase);
  }
}

template <class C> void StandaloneGraphTool<C>::runVerticesStep() {
  auto *Self = this;
  std::vector<std::thread> ThreadPool;

  // Divide the work over 'NCores' threads.
  for (unsigned i = 0; i < NCores; i++) {
    unsigned Start = i;
    std::thread t([Self, Start]() {
      unsigned index = Start;

      // Run computation for vertex i, i+NCores, i+2*NCores, etc.
      while (index < Self->Vertices.size()) {
        auto &Vertex = Self->Vertices[index];
        if (!Vertex.HaltVote) {
          // Each thread uses its own computation 'UserComputations[Start]'.
          Self->UserComputations[Start]->computePhase(
              &Vertex, Self->Phase, Self->getMessagesTo(Vertex.id()));
        }

        index += Self->NCores;
      }
    });
    ThreadPool.push_back(move(t));
  }

  // Main thread waits for all worker threads to finish.
  for (std::thread &t : ThreadPool) {
    t.join();
  }
}

template <class C> void StandaloneGraphTool<C>::applyGraphChanges() {
  // Remove messages from previous step.
  Messages.clear();

  // Send messages as requested. This overhead could be removed by adopting a
  // thread-safe data structure to queue messages in.
  for (auto &Vertex : Vertices) {
    for (const auto &Pair : Vertex.OutMessagesCache)
      sendMessages(Pair.first, Pair.second);
    Vertex.OutMessagesCache.clear();
  }

  // Remove requested edges.
  for (auto &Computation : UserComputations) {
    for (const auto &Pair : Computation->RemoveRequests)
      removeEdge(Pair.first, Pair.second);
    Computation->RemoveRequests.clear();
  }

  // Wake up vertices that got new messages.
  for (auto &Vertex : Vertices)
    if (!getMessagesTo(Vertex.id()).empty())
      Vertex.HaltVote = false;

  // Collect votes on whether the phasing iteration cycle should terminate.
  ReiterateResult = false;
  for (const auto &Vertex : Vertices)
    if (Vertex.ReiterateVote)
      ReiterateResult = true;
}

template <class C> bool StandaloneGraphTool<C>::phaseCompleted() {
  for (const auto &Vertex : Vertices) {
    if (!Vertex.HaltVote)
      return false;
  }
  return true;
}

/// Public API for building a graph and running a computation on that graph.
template <class UserComputation> class StandaloneGraphBuilder {
public:
  typedef ohmu::lsa::GraphTraits<UserComputation> Traits;
  typedef typename Traits::VertexValueType VertexValueType;
  typedef typename Traits::MessageValueType MessageValueType;
  typedef typename Traits::EdgeValueType EdgeValueType;
  typedef ohmu::lsa::GraphComputationFactory<UserComputation>
      GraphComputationFactory;
  typedef ohmu::lsa::GraphVertex<UserComputation> GraphVertex;

public:
  /// Adds a vertex with the specified identity and value. If the vertex already
  /// exists, that vertex' value is updated instead of creating a new vertex.
  void addVertex(const string &Id, const string &OhmuIR,
                 VertexValueType &Value) {
    Tool.addVertex(Id, OhmuIR, Value);
  }

  /// Adds an edge between two vertices with the specified value. If a vertex
  /// does not exist, it is created using the default constructor for its value.
  void addEdge(const string &Source, const string &Destination,
               const EdgeValueType &Value) {
    Tool.addEdge(Source, Destination, Value);
  }

  /// Returns the current set of vertices.
  const std::vector<GraphVertex> &getVertices() { return Tool.getVertices(); }

  /// Run the computation created by the specified factory.
  void run(GraphComputationFactory *Factory) { Tool.run(Factory); }

private:
  ohmu::lsa::StandaloneGraphTool<UserComputation> Tool;
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_STANDALONEGRAPHCOMPUTATION_H
