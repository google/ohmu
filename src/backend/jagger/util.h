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
void swap(T&& a, T&& b) {
  auto c = move(a);
  a = b;
  b = c;
}

struct TypedPtr {
  //__forceinline size_t set(size_t i, uchar type, uint data) const {
  //  this->type(i) = type;
  //  this->data(i) = data;
  //  return i + 1;
  //}
  __forceinline uchar& type(size_t i) const { return ((uchar*)root)[i]; }
  __forceinline uint& data(size_t i) const { return ((uint*)root)[i]; }
  __forceinline bool empty() const { return root == nullptr; }
  __forceinline struct TypedRef operator[](size_t i) const;
  explicit operator bool() const { return root != nullptr; }

  bool operator==(const TypedPtr& a) const { return root == a.root; }
  bool operator!=(const TypedPtr& a) const { return root != a.root; }

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
  bool operator==(const TypedRef& a) const { return p == a.p && i == a.i; }
  bool operator!=(const TypedRef& a) const { return p != a.p || i != a.i; }
  void operator++() { ++i; }
  uchar& type() const { return p.type(i); }
  uint& data() const { return p.data(i); }

  __forceinline void set(uchar type, uint data) const {
    p.type(i) = type;
    p.data(i) = data;
  }

 protected:
  friend TypedPtr;
  __forceinline TypedRef(TypedPtr p, size_t i) : p(p), i(i) {}
  TypedPtr p;
  size_t i;
};

inline TypedRef TypedPtr::operator[](size_t i) const {
  return TypedRef(*this, i);
}

template <typename Payload, size_t SIZE>
struct TypedStruct : TypedRef {
  enum { SLOT_COUNT = SIZE };
  typedef Payload Payload;
  __forceinline T field(size_t j) const {
    return TypedRef(p, j).as<T>();
  }
protected:
 __forceinline TypedRef init_(uchar type, Payload data) const {
   static_assert(sizeof(Payload) <= sizeof(p.data(i)),
                 "Can't cast to object of larger size.");
   p.type(i) = type;
   *(Payload*)&p.data(i) = data;
   return p[i + SLOT_COUNT];
 }
};

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

  TypedRef begin() const { return root[first]; }
  TypedRef end() const { return root[bound()]; }

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
  T& operator[](size_t i) const { return root[i]; }
  T* begin() const { return root; }
  T* end() const { return root + size_; }
  T& last() const { return root[size_ - 1]; }
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
    return *this;
  }
  ~Array() { if (root) delete[] root; }
  void swap(Array& a) {
    jagger::swap(root, a.root);
    jagger::swap(size_, a.size_);
  }
  AddressRange<T> slice(size_t first, size_t bound) const {
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
