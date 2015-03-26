//===- test_base.cpp -------------------------------------------*- C++ --*-===//
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
//
// Test code for class in base.
//
//===----------------------------------------------------------------------===//

#include "base/LLVMDependencies.h"
#include "base/MemRegion.h"
#include "base/ArrayTree.h"

#include <vector>

using namespace ohmu;


class UnMoveableItem {
public:
  UnMoveableItem() : uniqueHandle(0) { }
  UnMoveableItem(unsigned h) : uniqueHandle(h) { }
  ~UnMoveableItem() { uniqueHandle = 0; }

  unsigned getHandle() { return uniqueHandle; }

private:
  UnMoveableItem(const UnMoveableItem& i) = delete;
  UnMoveableItem(UnMoveableItem&& i) = delete;

  void operator=(const UnMoveableItem& i) = delete;
  void operator=(UnMoveableItem&& i) = delete;

private:
  unsigned uniqueHandle;
};


void error(const char* msg) {
  std::cerr << msg;
  assert(false && "Test failed.");
}


void testTreeArray() {
  MemRegion region;
  MemRegionRef arena(&region);
  ArrayTree<UnMoveableItem> atree;
  std::vector<UnMoveableItem*> items;

  unsigned i = 0;
  unsigned n = 1024;

  for (i = 0; i < n; ++i) {
    atree.emplace_back(arena, i);
    items.push_back(&atree.back());
  }

  for (i = 0; i < n; ++i) {
    // std::cerr << i << ",";
    if (atree[i].getHandle() != i)
      error("Error: ArrayTree construction failed.\n");
  }

  i = 0;
  for (auto& H : atree) {
    // std::cerr << i << ",";
    if (H.getHandle() != i)
      error("Error: ArrayTree iterator failed.\n");
    ++i;
  }
  if (i != n)
    error("Error: ArrayTree iteration failed.\n");

  i = n;
  for (auto & H : atree.reverse()) {
    if (H.getHandle() != i-1)
      error("Error: ArrayTree reverse iterator failed.\n");
    --i;
  }
  if (i != 0)
    error("Error: ArrayTree reverse iteration failed.\n");

  unsigned n2 = n + 2713;
  atree.resize(arena, n2, 42);
  for (i = n; i < n2; ++i) {
    if (atree[i].getHandle() != 42)
      error("Error: ArrayTree construction failed.\n");
  }

  unsigned n3 = n*4;
  atree.resize(arena, n3, 43);
  for (i = n2; i < n3; ++i) {
    if (atree[i].getHandle() != 43)
      error("Error: ArrayTree construction failed.\n");
  }

  atree.clear();
  for (auto *H : items) {
    if (H->getHandle() != 0)
      error("Error: ArrayTree clear failed.\n");
  }
}



int main(int argc, char** argv) {
  testTreeArray();
  return 0;
}

