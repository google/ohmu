#ifndef OHMU_LSA_GRAPHDESERIALIZER_H
#define OHMU_LSA_GRAPHDESERIALIZER_H

#include "lsa/StandaloneGraphComputation.h"
#include "til/Bytecode.h"

namespace ohmu {
namespace lsa {

template <class UserComputation>
class GraphDeserializer {
public:
  static void read(const std::string& FileName,
                   StandaloneGraphBuilder<UserComputation> *Builder) {
    ohmu::MemRegion Arena;
    ohmu::til::BytecodeFileReader ReadStream(FileName,
        ohmu::MemRegionRef(&Arena));

    int32_t NFunc = ReadStream.readInt32();
    for (unsigned i = 0; i < NFunc; i++) {
      std::string Function = ReadStream.readString();
      std::string OhmuIR = ReadStream.readString();
      typename GraphTraits<UserComputation>::VertexValueType Value;
      Builder->addVertex(Function, OhmuIR, Value);

      int32_t NNodes = ReadStream.readInt32();
      for (unsigned n = 0; n < NNodes; n++) {
        std::string Call = ReadStream.readString();
        Builder->addCall(Function, Call);
      }
    }
  }
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_GRAPHDESERIALIZER_H
