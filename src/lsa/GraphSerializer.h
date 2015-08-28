#ifndef OHMU_LSA_GRAPHSERIALIZER_H
#define OHMU_LSA_GRAPHSERIALIZER_H

#include "clang/Analysis/Til/Bytecode.h"
#include "lsa/BuildCallGraph.h"

namespace ohmu {
namespace lsa {

class GraphSerializer {
public:
  static void write(const std::string& FileName,
                    DefaultCallGraphBuilder *Builder) {
    ohmu::til::BytecodeFileWriter WriteStream(FileName);

    WriteStream.writeInt32(Builder->GetGraph().size());
    for (const auto &Pair : Builder->GetGraph()) {
      WriteStream.writeString(Pair.first);
      WriteStream.writeString(Pair.second->GetIR());
      WriteStream.writeInt32(Pair.second->GetCalls()->size());
      for (const std::string &Call : *Pair.second->GetCalls()) {
        WriteStream.writeString(Call);
      }
    }

    WriteStream.flush();
  }
};

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_GRAPHSERIALIZER_H
