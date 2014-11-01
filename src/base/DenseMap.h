//===- DenseMap.h ----------------------------------------------*- C++ --*-===//
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
//
// Wrapper around std::unordered_map for source compatibility with llvm.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_DENSEMAP_H
#define OHMU_DENSEMAP_H

#include <unordered_map>


namespace ohmu {

template<class K, class V>
class DenseMap {
public:
  typedef typename std::unordered_map<K,V>::iterator iterator;
  typedef typename std::unordered_map<K,V>::const_iterator const_iterator;

  iterator       begin()       { return map_.begin(); }
  const_iterator begin() const { return map_.begin(); }
  iterator       end()         { return map_.end();   }
  const_iterator end() const   { return map_.end();   }

  iterator find(const K& k) {
    return map_.find(k);
  }
  const_iterator find(const K& k) const {
    return map_.find(k);
  }

  void insert(const std::pair<K, V> &KV) {
    map_.insert(KV);
  }
  void insert(std::pair<K, V> &&KV) {
    map_.insert(KV);
  }

  void shrink_and_clear() { map_.clear(); }

private:
  std::unordered_map<K, V> map_;
};

}  // end namespace ohmu

#endif
