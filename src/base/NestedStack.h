//===- NestedStack.h -------------------------------------------*- C++ --*-===//
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


#ifndef OHMU_BASE_NESTEDSTACK_H_
#define OHMU_BASE_NESTEDSTACK_H_

#include "Util.h"

#include <vector>

namespace ohmu {


/// NestedStack implements a logical "stack of stacks".
/// There is a topmost stack of elements, which is visible, and allows
/// elements to be pushed and popped as usual.
/// The topmost stack can be saved, which will create a new, empty topmost
/// stack.  The original stack can then be restored later.
/// The implementation uses a single contiguous array for all logical stacks.
template <class T>
class NestedStack {
public:
  NestedStack() : Start(0) { }

  /// Push a new element onto the topmost stack.
  void push_back(const T& Elem) { Elements.push_back(Elem); }

  /// Pop an element off of the topmost stack.
  void pop_back() {
    assert(size() > 0 && "Cannot pop off of empty stack!.");
    Elements.pop_back();
  }

  /// Return top element on topmost stack.
  const T& back() const { return Elements.back(); }
  T&       back()       { return Elements.back(); }

  /// Return the i^th element on the topmost stack.
  const T& at(unsigned i) const { return Elements.at(Start + i); }
  T&       at(unsigned i)       { return Elements.at(Start + i); }

  /// Return size of topmost stack
  unsigned size() const { return Elements.size() - Start; }

  /// Return true if topmost stack is empty.
  bool empty() const { return size() == 0; }

  /// Return all elements in topmost stack.
  ArrayRef<T> elements() {
    T* pbegin = &Elements[Start];
    return ArrayRef<T>(pbegin, size());
  }

  /// Clear all elements from topmost stack.
  void clear() {
    for (unsigned i=0,n=size(); i < n; ++i)
      Elements.pop_back();
  }

  /// Save topmost stack, and return an id that can be used to restore it.
  /// Clear topmost stack.
  unsigned save() {
    unsigned Sv = Start;
    Start = Elements.size();
    return Sv;
  }

  /// Restore a stack that was previously saved.
  void restore(unsigned SaveID) {
    assert(size() == 0 && "Must clear stack before restoring!");
    Start = SaveID;
  }

private:
  std::vector<T> Elements;  //< Array of elements.
  unsigned       Start;     //< Index of first elem in topmost stack.
};



}  // end namespace ohmu

#endif  // SRC_BASE_NESTEDSTACK_H_
