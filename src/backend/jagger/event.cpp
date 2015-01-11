//===- normalize.cpp -------------------------------------------*- C++ --*-===//
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

#include "types.h"

namespace jagger {
namespace wax {
uint postTopologicalSort(Block* blocks, uint* neighbors, uint i, uint id) {
  blocks[i].blockID = id;
  if (blocks[blocks[i].dominator].blockID == INVALID_INDEX)
    id = postTopologicalSort(blocks, neighbors, blocks[i].dominator, id);
  for (auto j : blocks[i].predecessors(neighbors))
    if (blocks[j].blockID == INVALID_INDEX)
      id = postTopologicalSort(blocks, neighbors, j, id);
  return (blocks[i].blockID = id) + 1;
}

uint topologicalSort(Block* blocks, uint* neighbors, uint i, uint id) {
  blocks[i].blockID = id;
  for (auto j : blocks[i].successors(neighbors))
    if (blocks[j].blockID == INVALID_INDEX)
      id = topologicalSort(blocks, neighbors, j, id);
  return blocks[i].blockID = --id;
}

void sortBlocks(Block* target, Block* blocks, size_t size) {
  for (size_t i = 0; i != size; ++i)
    target[blocks[i].blockID] = blocks[i];
}

void updateNeighbors(uint* neighbors, Block* blocks, size_t numNeighbors) {
  for (size_t i = 0; i != numNeighbors; ++i)
    neighbors[i] = blocks[neighbors[i]].blockID;
}

void computeDominator(Block* blocks, uint* neighbors, Block& block) {
  block.domTreeSize = 1;
  if (!block.predecessors.size()) {
    block.dominator = INVALID_INDEX;
    return;
  }
  uint dominator = INVALID_INDEX;
  for (auto j : block.predecessors(neighbors)) {
    if (blocks[j].blockID >= block.blockID) continue;
    auto candidate = j;
    if (dominator == INVALID_INDEX) dominator = candidate;
    while (dominator != candidate)
      if (blocks[candidate].blockID > blocks[dominator].blockID)
        candidate = blocks[candidate].dominator;
      else
        dominator = blocks[dominator].dominator;
  }
  block.dominator = dominator;
}

void computePostDominator(Block* blocks, uint* neighbors, Block& block) {
  block.postDomTreeSize = 1;
  if (!block.successors.size()) {
    block.postDominator = INVALID_INDEX;
    return;
  }
  uint postDominator = INVALID_INDEX;
  for (auto j : block.successors(neighbors)) {
    if (blocks[j].blockID <= block.blockID) continue;
    auto candidate = j;
    if (postDominator == INVALID_INDEX) postDominator = candidate;
    while (postDominator != candidate)
      if (blocks[candidate].blockID < blocks[postDominator].blockID)
        candidate = blocks[candidate].postDominator;
      else
        postDominator = blocks[postDominator].postDominator;
  }
  block.postDominator = postDominator;
}

void computePostDomTreeSize(Block* blocks, Block& block) {
  if (block.postDominator == INVALID_INDEX) return;
  block.postDomTreeID = blocks[block.postDominator].postDomTreeSize;
  blocks[block.postDominator].postDomTreeSize += block.postDomTreeSize;
}

void computeDomTreeSize(Block* blocks, Block& block) {
  if (block.dominator == INVALID_INDEX) return;
  block.domTreeID = blocks[block.dominator].domTreeSize;
  blocks[block.dominator].domTreeSize += block.domTreeSize;
}

void computePostDomTreeID(Block* blocks, Block& block) {
  if (block.postDominator == INVALID_INDEX)
    block.postDomTreeID = 0;
  else
    block.postDomTreeID += blocks[block.postDominator].postDomTreeID;
}

void computeDomTreeID(Block* blocks, Block& block) {
  if (block.dominator == INVALID_INDEX)
    block.domTreeID = 0;
  else
    block.domTreeID += blocks[block.dominator].domTreeID;
}

void computeLoopDepth(Block* blocks, uint* neighbors, Block& block) {
  if (block.dominator == INVALID_INDEX) {
    block.loopDepth = 0;
    return;
  }
  block.loopDepth = blocks[block.dominator].loopDepth;
  for (auto j : block.predecessors(neighbors))
    if (block.dominates(blocks[j])) {
      block.loopDepth++;
      return;
    }
}

void Module::computeDominators() {
  Array<Block> blockSwapArray(blockArray.size);
  uint blockID = blockArray.size;
  // TODO: this is all kinds of buggy
  auto blocks = blockArray.begin() ;
  auto neighbors = neighborArray.begin();
  size_t numBlocks = blockArray.size;

  // TODO: remove unused blocks and update the block array size

  // Compute dominators.
  for (auto& fun : functionArray)
    blockID = topologicalSort(blocks, neighbors, fun.blocks.first, blockID);

  updateNeighbors(neighbors, blocks, neighborArray.size);
  sortBlocks(blockSwapArray.begin(), blockArray.begin(), blockArray.size);
  blockArray.swap(blockSwapArray);
  blocks = blockArray.begin();
  for (auto& block : blockArray)
    computeDominator(blocks, neighbors, block);
  // TODO: Fold this somewhere.
  for (auto& block : blockArray)
    block.blockID = INVALID_INDEX;

  // Compute post-dominators.
  for (auto& fun : functionArray)
    blockID = postTopologicalSort(blocks, neighbors, fun.blocks.bound - 1, 0);
  updateNeighbors(neighbors, blocks, neighborArray.size);
  sortBlocks(blockSwapArray.begin(), blockArray.begin(), blockArray.size);
  blockArray.swap(blockSwapArray);
  blocks = blockArray.begin();
  for (auto& block : blockArray.reverse()) {
    computePostDominator(blocks, neighbors, block);
    computeDomTreeSize(blocks, block);
  }
  for (auto& block : blockArray) {
    computePostDomTreeSize(blocks, block);
    computeDomTreeID(blocks, block);
    // Compute loop depth. TODO: make me more efficient
    if (block.dominator == INVALID_INDEX) {
      block.loopDepth = 0;
      continue;
    }
    block.loopDepth = blocks[block.dominator].loopDepth;
    for (auto i : block.predecessors(neighbors))
      if (block.dominates(blocks[i])) {
        block.loopDepth++;
        break;
      }
  }
  for (auto& block : blockArray.reverse()) computePostDomTreeID(blocks, block);
}
}  // namespace wax
}  // namespace jagger

#if 0
namespace Core {
namespace {

struct LiveRange {
  struct Iterator {
    struct EventRef {
      __forceinline EventRef(uchar& code, uint& data, const Iterator& i)
          : code(code), data(data), i(i) {}
      uchar& code;
      uint& data;
      const Iterator& i;
    };

    __forceinline Iterator(EventBuilder builder, size_t index)
        : builder(builder), index(index), skipUntil(index + 1) {}
    __forceinline EventRef operator*() const {
      return EventRef(builder.kind(index), builder.data(index), *this);
    }
    Iterator& operator++() {
      if (builder.kind(index) == JOIN_HEADER)
        skipUntil = builder.data(index);
      else if (builder.kind(index) == CASE_HEADER && notSkipping())
        index = builder.data(index);
      index--;
      return *this;
    }
    __forceinline bool operator!=(const Iterator& a) const {
      return index != a.index;
    }
    __forceinline bool notSkipping() const { return index < skipUntil; }

    EventBuilder builder;
    size_t index;
    size_t skipUntil;
  };

  LiveRange(EventBuilder builder, size_t def, size_t use)
      : builder(builder), def(def), use(use) {}
  Iterator begin() const { return Iterator(builder, use - 1); }
  Iterator end() const { return Iterator(EventBuilder(nullptr), def); }

 private:
  EventBuilder builder;
  size_t def;
  size_t use;
};
}  // namespace

void normalize(const EventList& in) {
  // Determine last uses.
  EventBuilder builder = in.builder;
  auto isOnlyUse = new uchar[in.numEvents] - in.first;
  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.kind(i) != LAST_USE) continue;
    size_t target = builder.data(i);
    isOnlyUse[i] = 1;
    for (auto other : LiveRange(builder, target, i))
      if (other.code == LAST_USE && other.data == target) {
        other.code = USE;
        if (other.i.notSkipping()) {
          isOnlyUse[i] = 0;
          break;
        }
      }
  }
  // Set upgrade last uses to only uses where possible.
  for (size_t i = in.first, e = in.bound(); i != e; ++i)
    if (builder.kind(i) == LAST_USE && isOnlyUse[i]) builder.kind(i) = ONLY_USE;
  delete[](isOnlyUse + in.first);

  // Commute commutable operations to save copies.
  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.kind(i) != ADD) continue;
    if (builder.kind(i - 2) == USE && builder.kind(i - 1) != USE) {
      builder.kind(i - 2) = builder.kind(i - 1);
      builder.kind(i - 1) = USE;
      auto temp = builder.data(i - 2);
      builder.data(i - 2) = builder.data(i - 1);
      builder.data(i - 1) = temp;
    }
  }
}

void sort(const EventList& in) {
  EventBuilder builder = in.builder;
  for (size_t i = 0, e = in.numEvents, j = in.first; i != e; j++, i++) {
    in.offsets[i].key = builder.kind(j);
    in.offsets[i].value = (unsigned)j;
  }
  std::stable_sort(in.offsets, in.offsets + in.numEvents);
}

void eliminatePhis(const EventList& in) {
  EventBuilder builder = in.builder;
  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.kind(i) != JOIN_COPY || builder.kind(i - 1) == USE) continue;
    size_t phi = (size_t)builder.data(i);
    if (builder.data(phi) < builder.data(i - 1))
      builder.data(phi) = builder.data(i - 1);
  }

  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.kind(i) != PHI) continue;
    if (auto j = builder.data(i)) {
      while (builder.data(j)) j = builder.data(j);
      builder.data(i) = j;
    }
  }
}
}  // namespace Jagger
#endif
