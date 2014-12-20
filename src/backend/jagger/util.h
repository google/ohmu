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
  uchar& type(size_t i) const { return ((uchar*)root)[i]; }
  uint& data(size_t i) const { return ((uint*)root)[i]; }
  bool empty() const { return root == nullptr; }
  struct TypedRef operator [](size_t i) const;

 private:
  friend struct TypedArray;
  explicit TypedPtr(char* root) : root(root) {}
  char* root;
};

struct TypedRef {
  TypedRef(TypedPtr p, size_t i) : p(p), i(i) {}
  template <typename T>
  T as() const {
    return *(const T*)this;
  }
  static size_t slotCount() { return 1; }
  TypedPtr p;
  size_t i;
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
