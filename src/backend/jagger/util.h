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

namespace Jagger {
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
  explicit TypedArray(size_t size) : p(nullptr), size(size) {
    first = (size + 2) / 3;
    auto buffer = new uint[(first * 3 + 3) / 4 + size];
    p = TypedPtr((char*)(buffer - first / 4));
  }
  TypedArray(const TypedArray&) = delete;
  TypedArray& operator=(const TypedArray&) = delete;
  ~TypedArray() { delete[](&p[0] + first / 4); }
  size_t bound() const { return first + size; }

  TypedRef begin() const { return TypedRef(p, first); }
  TypedRef end() const { return TypedRef(p, bound()); }

  TypedPtr p;
  size_t size;
  size_t first;
};
} // namespace Jagger

// struct Sort {
//  uint key, value;
//  bool operator <(const Sort& a) const { return key < a.key; }
//};
// unsigned prefixBuffer[NUM_OPCODES + 1];
// unsigned* prefix;
// Sort* offsets;
// Sort* scratch;
