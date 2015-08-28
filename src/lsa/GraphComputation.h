//===- GraphComputation.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
// The purpose of this level of indirection is to easily replace the standalone
// framework with a different framework providing the same GraphComputation and
// GraphVertex interface.
//===----------------------------------------------------------------------===//

#ifndef OHMU_LSA_GRAPHCOMPUTATION_H
#define OHMU_LSA_GRAPHCOMPUTATION_H

#include "StandaloneGraphComputation.h"

/// To provide serialization in Google's Pregel framework.
template <class T> class StringCoderCustom;

namespace ohmu {
namespace lsa {

// Adapted from til/Bytecode.cpp:
static void writeUInt64ToString(uint64_t V, string *result) {
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

static uint64_t readUInt64FromString(const string &str, int &index) {
  uint64_t V = 0;
  for (unsigned B = 0; B < 64; B += 7) {
    uint64_t Byt = str[index++];
    V = V | ((Byt & 0x7Fu) << B);
    if ((Byt & 0x80) == 0)
      break;
  }
  return V;
}

} // namespace lsa
} // namespace ohmu

#endif // OHMU_LSA_GRAPHCOMPUTATION_H
