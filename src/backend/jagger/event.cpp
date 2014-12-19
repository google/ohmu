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
#include <algorithm>
#include <stdio.h>

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
