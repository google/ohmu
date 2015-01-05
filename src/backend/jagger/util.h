//===- util.h --------------------------------------------------*- C++ --*-===//
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

#pragma once
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace jagger {
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

struct TypedPtr {
  __forceinline size_t set(size_t i, uchar type, uint data) const {
    this->type(i) = type;
    this->data(i) = data;
    return i + 1;
  }
  __forceinline uchar& type(size_t i) const { return ((uchar*)root)[i]; }
  __forceinline uint& data(size_t i) const { return ((uint*)root)[i]; }
  __forceinline bool empty() const { return root == nullptr; }
  __forceinline struct TypedRef operator[](size_t i) const;
  explicit operator bool() const { return root != nullptr; }

 private:
  friend struct TypedArray;
  explicit TypedPtr(char* root) : root(root) {}
  char* root;
};

struct TypedRef {
  template <typename T>
  __forceinline T as() const {
    return *(const T*)this;
  }
  bool operator ==(const TypedRef& a) const { return i == a.i; }
  bool operator !=(const TypedRef& a) const { return i != a.i; }
  TypedRef& operator ++() { ++i; return *this; }
  TypedRef(TypedPtr p, size_t i) : p(p), i(i) {}
  TypedPtr p;
  size_t i;
};

template <typename Payload, size_t SIZE>
struct TypedStruct : TypedRef {
  __forceinline Payload& operator*() const {
    static_assert(sizeof(Payload) <= sizeof(p.data(i)),
                  "Can't cast to object of larger size.");
    return *(Payload*)&p.data(i);
  }
  __forceinline Payload* operator->() const { return (Payload*)&p.data(i); }
  __forceinline TypedRef pointee() const { return TypedRef(p, p.data(i)); }
  template <typename T>
  __forceinline T field(size_t j) const {
    return TypedRef(p, j).as<T>();
  }
  __forceinline static size_t slotCount(size_t i = 0) { return SIZE + i; }
};

inline TypedRef TypedPtr::operator[](size_t i) const {
  return TypedRef(*this, i);
}

struct TypedArray {
  TypedArray() : root(nullptr) {}
  void init(size_t size) {
    if (root) delete[](&root[0] + first / 4);
    this->size = size;
    first = (size + 2) / 3;
    auto buffer = new uint[(first * 3 + 3) / 4 + size];
    root = TypedPtr((char*)(buffer - first / 4));
  }
  TypedArray(const TypedArray&) = delete;
  TypedArray& operator=(const TypedArray&) = delete;
  ~TypedArray() { if (root) delete[](&root[0] + first / 4); }
  size_t bound() const { return first + size; }

  TypedRef begin() const { return TypedRef(root, first); }
  TypedRef end() const { return TypedRef(root, bound()); }

  size_t size;
  size_t first;
  TypedPtr root;
};

template <typename T>
struct AddressRange {
  AddressRange(T* first, T* bound) : first(first), bound(bound) {}
  T* begin() const { return first; }
  T* end() const { return bound; }

private:
  T* first;
  T* bound;
};

template<typename T>
struct ReverseIterator {
  operator T() const { return i; }
  void operator ++() { --i; }
  T i;
};

struct Range {
  Range() {}
  Range(uint first, uint bound) : first(first), bound(bound) {}
  uint size() const { return bound - first; }
  template <typename T>
  AddressRange<T> operator()(T* p) const {
    return range(p, first, bound);
  }
  uint first;
  uint bound;
};

template <typename T>
struct Array {
  T& operator[](size_t i) const { return root[i]; }
  T* begin() const { return root + 1; }
  T* end() const { return root + 1 + size_; }
  T& last() const { return root[size_]; }
  explicit operator bool() const { return root != nullptr; }
  size_t size() const { return size_; }

  Array() : root(nullptr), size_(0) {}
  Array(size_t size) : root(new T[size + 1]), size_(size) {}
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  Array(Array&& a) : root(a.root), size_(a.size_) { a.root = nullptr; }
  Array& operator=(Array&& a) {
    if (this == &a) return *this;
    if (root) delete[] root;
    root = a.root;
    size_ = a.size_;
    a.root = nullptr;
    return *this;
  }
  ~Array() { if (root) delete[] root; }
  void swap(Array& a) {
    swap(root, a.root);
    swap(size_, a.size_);
  }

 private:
  T* root;
  size_t size_;
};

// TODO: move me to a cpp and remove the header requirement
void error(const char* format, ...) {
  va_list argList;
  va_start(argList, format);
  vprintf(format, argList);
  va_end(argList);
  exit(1);
}
}  // namespace jagger
