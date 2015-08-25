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

#endif // OHMU_LSA_GRAPHCOMPUTATION_H
