#ifndef OHMU_LSA_GRAPHCOMPUTATION_H
#define OHMU_LSA_GRAPHCOMPUTATION_H

/// To specify a pregel computation, include these defines before importing
/// GraphComputation.h
// #define GRAPH_COMPUTATION_MODE_PREGEL 1
// #define NAME_AS_STRING(name) #name
// #define PREGEL_GRAPH_COMPUTATION_PATH \
//   NAME_AS_STRING(path/to/PregelGraphComputation.h)

#if GRAPH_COMPUTATION_MODE_PREGEL
#include PREGEL_GRAPH_COMPUTATION_PATH
#else
#include "StandaloneGraphComputation.h"
#endif

/// To provide serialization in Google's Pregel framework.
template <class T> class StringCoderCustom;

#endif // OHMU_LSA_GRAPHCOMPUTATION_H
