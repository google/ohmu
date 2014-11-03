//===- ArrayTree.h ---------------------------------------------*- C++ --*-===//
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

#ifndef OHMU_BASE_ARRAYTREE_H
#define OHMU_BASE_ARRAYTREE_H


#include "MemRegion.h"
#include "Util.h"


namespace ohmu {


/// ArrayTree stores its elements in a 2-level "tree".   Rather than storing
/// elements in a contiguous array, it stores them in contiguous chunks of size
/// size 2^LeafSizeExponent.  The extra level of indirection is slower than a
/// normal array, but still offers O(1) random access operations, and has
/// good cache locality.  ArrayTree is slower than std::vector, but makes
/// better use of memory in 2 situations:
///
/// First, although it is dynamically resizeable, ArrayTree never moves its
/// elements, so pointers to elements are stable across resizes.  til::Future
/// depends on this capability.
///
/// Second, because it does not need to reallocate the entire array, ArrayTree
/// is suitable for use with bump pointer allocators.  The root node is the
/// only node that is reallocated.  Because memory is not reclaimed until the
/// whole region is released, a normal resizeable array is wasteful when used
/// with a bump allocator.
template <class T, unsigned LeafSizeExponent=3>
class ArrayTree {
public:
  /// The number of elements in each leaf node.
  static const unsigned LeafSize = (1 << LeafSizeExponent);
  static const unsigned DefaultInitialCapacity = 2 * LeafSize;

  ArrayTree()
      : Data(nullptr), Size(0), Capacity(0)
  { }
  ArrayTree(MemRegionRef A, unsigned Cap)
      : Data(nullptr), Size(0), Capacity(0)
  { reserve(A, Cap); }

  size_t size()     const { return Size; }
  size_t capacity() const { return Capacity; }

  T &at(unsigned i) {
    assert(i < Size && "Array index out of bounds.");
    return Data[rootIndex(i)][leafIndex(i)];
  }
  const T &at(unsigned i) const {
    assert(i < Size && "Array index out of bounds.");
    return Data[rootIndex(i)][leafIndex(i)];
  }
  T &back() {
    assert(Size > 0 && "No elements in the array.");
    return at(Size-1);
  }
  const T &back() const { return at(Size-1)
    assert(Size > 0 && "No elements in the array.");
    return at(Size-1);
  }
  T       &operator[](unsigned i)       { return at(i); }
  const T &operator[](unsigned i) const { return at(i); }


  /// Reserve space for at least Ncp items.
  void reserve(MemRegionRef A, unsigned Ncp);

  /// Push a new element onto the array.
  void push_back(MemRegionRef A, const T &Elem);

  template<class... Args>
  void emplace_back(MemRegionRef A, Args&&... args);

  /// Resize to Nsz, initializing newly-added elements to V
  template<class... Args>
  void resize(MemRegionRef A, unsigned Nsz, const Args&... args);

  /// drop last n elements from array
  void drop(unsigned Num) {
    assert(Size > Num);
    for (unsigned i=Size-Num,n=Size; i<n; ++i)
      Data[rootIndex(i)][leafIndex(i)].T::~T();
    Size -= Num;
  }

  /// drop all elements from array.
  void clear() {
    for (unsigned i=0,n=Size; i<n; ++i)
      Data[rootIndex(i)][leafIndex(i)].T::~T();
    Size = 0;
  }

  /// drop elements from array without calling destructors.
  void clearWithoutDestruct() { Size = 0; }

  /// drop elements from array without calling destructors.
  void dropWithoutDestruct(unsigned Num) {
    assert(Size > Num);
    Size -= Num;
  }


  class iterator {
  public:
    iterator(ArrayTree& Atr, unsigned i) : ATree(Atr), Idx(i) { }

    T& operator*() const { return ATree[Idx]; }

    const iterator& operator++() { ++Idx; return *this; }
    const iterator& operator--() { --Idx; return *this; }

    bool operator==(const iterator& I) const { return Idx == I.Idx; }
    bool operator!=(const iterator& I) const { return Idx != I.Idx; }

  private:
    ArrayTree &ATree;
    unsigned  Idx;    // Index into the TreeArray
  };

  class const_iterator : public iterator {
  public:
    const_iterator(const ArrayTree& Atr, unsigned i)
      : iterator(const_cast<ArrayTree&>(Atr), i) { }

    const T& operator*() const { return iterator::operator*(); }
  };

  class reverse_iterator : public iterator {
  public:
    reverse_iterator(ArrayTree& Atr, unsigned i) : iterator(Atr, i) { }

    const reverse_iterator& operator++() {
      iterator::operator--();
      return *this;
    }
    const reverse_iterator& operator--() {
      iterator::operator++();
      return *this;
    }
  };

  class cr_iterator : public const_iterator {
  public:
    cr_iterator(const ArrayTree& Atr, unsigned i)
      : const_iterator(Atr, i) {}

    const cr_iterator& operator++() {
      const_iterator::operator--();
      return *this;
    }
    const cr_iterator& operator--() {
      const_iterator::operator++();
      return *this;
    }
  };

  typedef cr_iterator const_reverse_iterator;


  iterator         begin()        { return iterator(*this, 0); }
  iterator         end()          { return iterator(*this, Size); }
  const_iterator   begin()  const { return const_iterator(*this, 0); }
  const_iterator   end()    const { return const_iterator(*this, Size); }

  reverse_iterator rbegin()       { return reverse_iterator(*this, Size-1); }
  reverse_iterator rend()         { return reverse_iterator(*this, MinusOne); }
  cr_iterator      rbegin() const { return cr_iterator(*this, Size-1); }
  cr_iterator      rend()   const { return cr_iterator(*this, MinusOne); }


  // An adaptor to reverse a simple array
  class ReverseAdaptor {
   public:
    ReverseAdaptor(ArrayTree &A) : ATree(A) {}

    reverse_iterator begin()       { return ATree.rbegin(); }
    reverse_iterator end()         { return ATree.rend();   }
    cr_iterator      begin() const { return ATree.rbegin(); }
    cr_iterator      end()   const { return ATree.rend();   }

   private:
    ArrayTree &ATree;
  };

  ReverseAdaptor reverse() const { return ReverseAdaptor(*this); }
  ReverseAdaptor reverse()       { return ReverseAdaptor(*this); }

private:
  static const unsigned MinusOne = static_cast<unsigned>(-1);

  // std::max is annoying here, because it requires a reference,
  // thus forcing InitialCapacity to be initialized outside the .h file.
  unsigned u_max(unsigned i, unsigned j) { return (i < j) ? j : i; }

  static unsigned rootIndex(unsigned i) { return (i >> LeafSizeExponent); }
  static unsigned leafIndex(unsigned i) { return (i & (LeafSize - 1));    }

  /// Reserve space for a new leaf.
  void reserveLeaf(MemRegionRef A);

  ArrayTree(const ArrayTree &A) LLVM_DELETED_FUNCTION;

  T **Data;
  unsigned Size;
  unsigned Capacity;
};



template<class T, unsigned LeafSizeExponent>
void ArrayTree<T, LeafSizeExponent>::reserve(MemRegionRef A, unsigned Ncp) {
  if (Ncp <= Capacity)
    return;
  // std::cerr << "===========================\nReserve " << Ncp << ".\n";

  unsigned RtSize  = rootIndex(Capacity);
  unsigned NRtSize = rootIndex(Ncp);
  if (leafIndex(Ncp) > 0)
    ++NRtSize;
  Ncp = NRtSize << LeafSizeExponent;

  T** NData = A.allocateT<T*>(NRtSize);
  memcpy(NData, Data, RtSize * sizeof(T*));
  memset(NData + RtSize, 0, (NRtSize - RtSize) * sizeof(T*));
  Data     = NData;
  Capacity = Ncp;
}


template<class T, unsigned Exp>
void ArrayTree<T, Exp>::reserveLeaf(MemRegionRef A) {
  unsigned i = rootIndex(Size);
  if (Data[i])
    return;
  // std::cerr << "ReserveLeaf.\n";
  Data[i] = A.allocateT<T>(LeafSize);
}


template<class T, unsigned Exp>
void ArrayTree<T, Exp>::push_back(MemRegionRef A, const T &Elem) {
  unsigned i = Size;
  if (i >= Capacity)
    reserve(A, u_max(DefaultInitialCapacity, Capacity*2));
  if (leafIndex(i) == 0)
    reserveLeaf(A);
  Data[rootIndex(i)][leafIndex(i)] = Elem;
  ++Size;
}


template<class T, unsigned Exp>
template<class... Args>
void ArrayTree<T, Exp>::emplace_back(MemRegionRef A, Args&&... args) {
  unsigned i = Size;
  if (i >= Capacity)
    reserve(A, u_max(DefaultInitialCapacity, Capacity*2));
  if (leafIndex(i) == 0)
    reserveLeaf(A);
  new (&Data[rootIndex(i)][leafIndex(i)]) T(args...);
  ++Size;
}


template<class T, unsigned Exp>
template<class... Args>
void ArrayTree<T, Exp>::resize(MemRegionRef A, unsigned Nsz,
                               const Args&... args) {
  if (Nsz <= Size)
    return;

  if (Nsz > Capacity)
    reserve(A, Nsz);

  // Allocate new leaf nodes
  unsigned ri = rootIndex(Size);
  unsigned rn = rootIndex(Nsz);
  if (leafIndex(Nsz) > 0)
    ++rn;
  for (; ri < rn; ++ri) {
    if (!Data[ri])
      Data[ri] = A.allocateT<T>(LeafSize);
  }

  // Emplace new data items
  for (unsigned i = Size; i < Nsz; ++i)
    new (&Data[rootIndex(i)][leafIndex(i)]) T(args...);
  Size = Nsz;
}



}  // end namespace ohmu

#endif  // OHMU_BASE_ARRAYTREE_H
