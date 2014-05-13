//===- MemRegion.h ---------------------------------------------*- C++ --*-===//
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
// MemRegion is a class which does bump pointer allocation.  Everything
// allocated in the region will be destroyed when the region is destroyed.
//
// MemRegionRef stores a reference to a region.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_MEMREGION_H
#define OHMU_MEMREGION_H

#include <cstdlib>
#include <iostream>
#include <iomanip>

namespace ohmu {


class MemRegion {
public:
  // Create a new MemRegion
  MemRegion();

  // Destroy a MemRegion, along with all data that was allocated in it.
  ~MemRegion();

  // Pad sizes out to nearest 8 byte boundary.
  inline unsigned getAlignedSize(unsigned size) {
    if ((size & 0x7) == 0)
      return size;
    else
      return ((size >> 3) + 1) << 3;
  }

  template <class T>
  inline T* allocateT() {
    return reinterpret_cast<T*>(allocate(sizeof(T)));
  }

  template <class T>
  inline T* allocateT(size_t nelems) {
    return reinterpret_cast<T*>(allocate(sizeof(T) * nelems));
  }

  // Allocate memory for a new object from the pool.
  // Small objects are bump allocated; large ones are not.
  inline void* allocate(size_t size) {
    // std::cerr << " " << size;
    size = getAlignedSize(size);
    if (size <= maxBumpAllocSize)
      return allocateSmall(size);
    else
      return allocateLarge(size);
  }

  // No-op.
  void deallocate(void* ptr) { }

  inline void* allocateSmall(size_t size) {
    if (currentPosition_ + size >= currentBlockEnd_)
      grabNewBlock();

    void* result = currentPosition_;
    currentPosition_ += size;
    return result;
  }

  inline void* allocateLarge(size_t size) {
    // std::cerr << "\nallocLarge " << size;
    char* p = reinterpret_cast<char*>(malloc(size + headerSize));
    linkBack(largeBlocks_, p);
    return p + headerSize;
  }

  void grabNewBlock();

private:
  static const unsigned defaultBlockSize  = 4096;  // 4kb blocks
  static const unsigned maxBumpAllocSize  = 512;   // 8 allocs per block
  static const unsigned headerSize        = sizeof(void*);

  void linkBack(char*& blockPointer, char* newBlock) {
    *reinterpret_cast<char**>(newBlock) = blockPointer;
    blockPointer = newBlock;
  }

  char* currentBlock_;      // current bump allocation block
  char* currentBlockEnd_;
  char* currentPosition_;

  char* largeBlocks_;       // linked list of large blocks
};


// Simple wrapper class which holds a pointer to a region.
class MemRegionRef {
public:
  MemRegionRef() : allocator_(nullptr) {}
  MemRegionRef(MemRegion *region) : allocator_(region) { }

  void *allocate(size_t sz) {
    return allocator_->allocate(sz);
  }

  template <typename T> T *allocateT() {
    return allocator_->allocateT<T>();
  }

  template <typename T> T *allocateT(size_t nelems) {
    return allocator_->allocateT<T>(nelems);
  }

private:
  MemRegion* allocator_;
};


}  // end namespace ohmu


inline void* operator new(size_t size, ohmu::MemRegionRef& region) {
  return region.allocate(size);
}


#endif  // OHMU_MEMREGION_H
