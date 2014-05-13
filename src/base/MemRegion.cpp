//===- MemRegion.cpp -------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//

#include "MemRegion.h"

namespace ohmu {

MemRegion::MemRegion()
    : currentBlock_(0), currentBlockEnd_(0), currentPosition_(0),
      largeBlocks_(0) {
  grabNewBlock();
}


inline void freeList(char* p) {
  while (p) {
    // std::cerr << ".";
    // Each block has a pointer to the previous block at the start
    char* np = *reinterpret_cast<char**>(p);
    free(p);
    p = np;  // pun intended.
  }
  // std::cerr << "\n";
}


MemRegion::~MemRegion() {
  // std::cerr << "\nfree[" << std::hex << reinterpret_cast<size_t>(this) << "]";
  freeList(currentBlock_);
  // std::cerr << "\nfree[]";
  freeList(largeBlocks_);
}


void MemRegion::grabNewBlock() {
  // std::cerr << "\nallocBlock[" << std::hex << reinterpret_cast<size_t>(this) << "]";

  // FIXME: ideally, we'd like to allocate exact memory pages.
  // If defaultBlockSize=4096, and malloc adds headers of its own, then we
  // may be over page size.
  char* newBlock = reinterpret_cast<char*>(malloc(defaultBlockSize));
  linkBack(currentBlock_, newBlock);

  currentPosition_ = newBlock + headerSize;
  currentBlockEnd_ = newBlock + defaultBlockSize;
}


}  // end namespace ohmu
