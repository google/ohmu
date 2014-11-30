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
#include <stdio.h>

namespace Jagger {

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
      return EventRef(builder.code(index), builder.data(index), *this);
    }
    Iterator& operator++() {
      if (builder.code(index) == JOIN_HEADER)
        skipUntil = builder.data(index);
      else if (builder.code(index) == CASE_HEADER && notSkipping())
        index = builder.data(index);
      index--;
      return *this;
    }
    __forceinline bool operator!=(const Iterator& a) const { return index != a.index; }
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

void normalize(const EventList& in) {
  // Determine last uses.
  EventBuilder builder = in.builder;
  auto isOnlyUse = new uchar[in.numEvents] - in.first;
  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.code(i) != LAST_USE) continue;
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
  for (size_t i = in.first, e = in.bound(); i != e; ++i)
    if (builder.code(i) == LAST_USE && isOnlyUse[i]) builder.code(i) = ONLY_USE;
  delete[](isOnlyUse + in.first);

  for (size_t i = in.first, e = in.bound(); i != e; ++i) {
    if (builder.code(i) != JOIN_COPY || builder.code(i - 1) == USE) continue;
    size_t phi = (size_t)builder.data(i);
    if (builder.data(phi) > builder.data(i - 1))
      builder.data(phi) = builder.data(i - 1);
  }
}

}  // namespace Jagger