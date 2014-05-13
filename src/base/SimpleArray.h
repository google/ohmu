//===- SimpleArray.h -------------------------------------------*- C++ --*-===//
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

#ifndef OHMU_SIMPLEARRAY_H
#define OHMU_SIMPLEARRAY_H

#include "Util.h"


namespace ohmu {

// A simple fixed size array class that does not manage its own memory,
// suitable for use with bump pointer allocation.
template <class T> class SimpleArray {
public:
  SimpleArray() : Data(nullptr), Size(0), Capacity(0) {}
  SimpleArray(T *Dat, size_t Cp, size_t Sz = 0)
      : Data(Dat), Size(Sz), Capacity(Cp) {}
  SimpleArray(MemRegionRef A, size_t Cp)
      : Data(Cp == 0 ? nullptr : A.allocateT<T>(Cp)), Size(0), Capacity(Cp) {}
  SimpleArray(SimpleArray<T> &&A)
      : Data(A.Data), Size(A.Size), Capacity(A.Capacity) {
    A.Data = nullptr;
    A.Size = 0;
    A.Capacity = 0;
  }

  SimpleArray &operator=(SimpleArray &&RHS) {
    if (this != &RHS) {
      Data = RHS.Data;
      Size = RHS.Size;
      Capacity = RHS.Capacity;

      RHS.Data = nullptr;
      RHS.Size = RHS.Capacity = 0;
    }
    return *this;
  }

  void reserve(size_t Ncp, MemRegionRef A) {
    if (Ncp < Capacity)
      return;
    T *Odata = Data;
    Data = A.allocateT<T>(Ncp);
    Capacity = Ncp;
    memcpy(Data, Odata, sizeof(T) * Size);
    return;
  }

  typedef T *iterator;
  typedef const T *const_iterator;

  size_t size() const { return Size; }
  size_t capacity() const { return Capacity; }

  T &operator[](unsigned i) {
    assert(i < Size && "Array index out of bounds.");
    return Data[i];
  }
  const T &operator[](unsigned i) const {
    assert(i < Size && "Array index out of bounds.");
    return Data[i];
  }

  iterator begin() { return Data; }
  iterator end() { return Data + Size; }

  const_iterator cbegin() const { return Data; }
  const_iterator cend() const { return Data + Size; }

  void push_back(const T &Elem) {
    assert(Size < Capacity);
    Data[Size++] = Elem;
  }

  void setValues(unsigned Sz, const T& C) {
    assert(Sz <= Capacity);
    Size = Sz;
    for (unsigned i = 0; i < Sz; ++i) {
      Data[i] = C;
    }
  }

  template <class Iter> unsigned append(Iter I, Iter E) {
    size_t Osz = Size;
    size_t J = Osz;
    for (; J < Capacity && I != E; ++J, ++I)
      Data[J] = *I;
    Size = J;
    return J - Osz;
  }

private:
  SimpleArray(const SimpleArray<T> &A) LLVM_DELETED_FUNCTION;

  T *Data;
  size_t Size;
  size_t Capacity;
};


}  // end namespace ohmu

#endif  // OHMU_SIMPLEARRAY_H
