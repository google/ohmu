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

namespace jagger {
typedef long long int64;
typedef unsigned long long uint64;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;

template <typename T>
T&& move(T& a) {
  return (T && )a;
}
template <typename T>
void swap(T& a, T& b) {
  T c = move(a);
  a = move(b);
  b = move(c);
}

struct TypedPtr {
  __forceinline uchar& type(size_t i) const { return ((uchar*)p)[i]; }
  __forceinline uint& data(size_t i) const { return ((uint*)p)[i]; }
  __forceinline bool empty() const { return p == nullptr; }
  __forceinline struct TypedRef operator[](size_t i) const;
  explicit operator bool() const { return p != nullptr; }

  bool operator==(const TypedPtr& a) const { return p == a.p; }
  bool operator!=(const TypedPtr& a) const { return p != a.p; }

 private:
  friend struct TypedArray;
  TypedPtr() : p(nullptr) {}
  char* p;
};

struct TypedRef {
  template <typename T>
  __forceinline T as() const {
    return *(const T*)this;
  }
  uchar& type() const { return p.type(i); }
  uint& data() const { return p.data(i); }
  uint index() const { return (uint)i; }

 protected:
  __forceinline TypedRef(TypedPtr p, size_t i) : p(p), i(i) {}
  TypedPtr p;
  size_t i;
  friend TypedPtr;
};

inline TypedRef TypedPtr::operator[](size_t i) const {
  return TypedRef(*this, i);
}

template <typename Payload, size_t SIZE>
struct TypedStruct : TypedRef {
  enum { SLOT_COUNT = SIZE };
  typedef Payload Payload;
  template <typename T>
  __forceinline T field(size_t j) const {
    return p[i + j].as<T>();
  }
  Payload& payload() const { return *(Payload*)&p.data(i); }

 protected:
  __forceinline TypedRef init_(uchar type, Payload data) const {
    static_assert(sizeof(Payload) <= sizeof(p.data(i)),
                  "Can't cast to object of larger size.");
    p.type(i) = type;
    payload() = data;
    return p[i + SIZE];
  }
};

struct TypedArray {
  struct Iterator {
    TypedRef operator*() const { return root[i]; }
    void operator++() { ++i; }
    bool operator!=(const Iterator& a) const { return i != a.i; }

   private:
    friend TypedArray;
    Iterator(TypedPtr root, size_t i) : root(root), i(i) {}
    TypedPtr root;
    size_t i;
  };

  TypedArray() : size(0), first(0) {}
  explicit TypedArray(size_t size) : size(size), first((size + 2) / 3) {
    if (!size) return;
    auto buffer = new uint[(first * 3 + 3) / 4 + size];
    root.p = (char*)(buffer - first / 4);
  }
  TypedArray(const TypedArray&) = delete;
  TypedArray& operator=(const TypedArray&) = delete;
  TypedArray(TypedArray&& a) : size(a.size), first(a.first), root(a.root) {
    a.size = 0;
    a.first = 0;
    a.root = TypedPtr();
  }
  TypedArray& operator=(TypedArray&& a) {
    if (this == &a) return *this;
    if (root) delete[](&root.data(0) + first / 4);
    size = a.size;
    first = a.first;
    root = a.root;
    a.size = 0;
    a.first = 0;
    a.root = TypedPtr();
    return *this;
  }
  ~TypedArray() {
    if (root) delete[](&root.data(0) + first / 4);
  }
  size_t bound() const { return first + size; }

  Iterator begin() const { return Iterator(root, first); }
  Iterator end() const { return Iterator(root, bound()); }

  size_t size;
  size_t first;
  TypedPtr root;
};

template <typename T>
struct ReverseAddressRange {
  struct Iterator {
    Iterator(T* p) : p(p) {}
    T& operator*() const { return *p; }
    bool operator!=(const Iterator& a) const { return p != a.p; }
    void operator++() { --p; }
    T* p;
  };
  ReverseAddressRange(T* first, T* bound)
      : first(bound - 1), bound(first - 1) {}
  Iterator begin() const { return first; }
  Iterator end() const { return bound; }

 private:
  T* first;
  T* bound;
};

template <typename T>
struct AddressRange {
  AddressRange(T* first, T* bound) : first(first), bound(bound) {}
  T* begin() const { return first; }
  T* end() const { return bound; }
  ReverseAddressRange<T> reverse() const {
    return ReverseAddressRange<T>(first, bound);
  }

private:
  T* first;
  T* bound;
};

struct Range {
  Range() {}
  Range(uint first, uint bound) : first(first), bound(bound) {}
  uint size() const { return bound - first; }
  template <typename T>
  AddressRange<T> operator()(T* p) const {
    return AddressRange<T>(p + first, p + bound);
  }
  uint first;
  uint bound;
};

static const uint INVALID_INDEX = (uint)-1;

template <typename T>
struct Array {
  T& operator[](size_t i) const { assert(i < size_); return root[i]; }
  T* begin() const { return root; }
  T* end() const { return root + size_; }
  T& last() const { return (*this)[size_ - 1]; }
  explicit operator bool() const { return root != nullptr; }
  size_t size() const { return size_; }

  Array() : root(nullptr), size_(0) {}
  explicit Array(size_t size) : root(new T[size]), size_(size) {}
  Array(const Array&) = delete;
  Array& operator=(const Array&) = delete;
  Array(Array&& a) : root(a.root), size_(a.size_) { a.root = nullptr; }
  Array& operator=(Array&& a) {
    if (this == &a) return *this;
    if (root) delete[] root;
    root = a.root;
    size_ = a.size_;
    a.root = nullptr;
    a.size_ = 0;
    return *this;
  }
  ~Array() {
    if (root) delete[] root;
  }
  AddressRange<T> slice(size_t first, size_t bound) const {
    if (first > size_) first = size_;
    if (bound > size_) bound = size_;
    return AddressRange<T>(root + first, root + size_);
  }
  AddressRange<T> slice(const Range& range) const {
    return range(root);
  }
  ReverseAddressRange<T> reverse() const {
    return ReverseAddressRange<T>(root, root + size_);
  }

private:
  T* root;
  size_t size_;
};

void error(const char* format, ...);
}  // namespace jagger
