//===- LLVMDependencies.h --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
//
// This file encapsulates all of the dependencies between ohmu and clang/llvm.
// The standalone version of ohmu provides its own verson of these definitions.
//
// The standalone version can be found at:
//
//    https://github.com/google/ohmu
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_BASE_LLVM_DEPENDENCIES_H
#define OHMU_BASE_LLVM_DEPENDENCIES_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

#define OHMU_STANDALONE


namespace ohmu {

template <class T, class U>
inline bool isa(const U* p) { return T::classof(p); }

template <class T, class U>
inline T* cast(U* p) {
  assert(T::classof(p));
  return static_cast<T*>(p);
}

template <class T, class U>
inline const T* cast(const U* p) {
  assert(T::classof(p));
  return static_cast<const T*>(p);
}

template <class T, class U>
inline T* dyn_cast(U* p) {
  if (!T::classof(p))
    return 0;
  return static_cast<T*>(p);
}

template <class T, class U>
inline const T* dyn_cast(const U* p) {
  if (!T::classof(p))
    return 0;
  return static_cast<const T*>(p);
}

template <class T, class U>
inline T* dyn_cast_or_null(U* p) {
  if (!p || !T::classof(p))
    return 0;
  return static_cast<T*>(p);
}

template <class T, class U>
inline const T* dyn_cast_or_null(const U* p) {
  if (!p || !T::classof(p))
    return 0;
  return static_cast<const T*>(p);
}



class StringRef {
public:
  StringRef(const char* s)
    : str_(s), len_(static_cast<unsigned>(strlen(s)))
  { }
  StringRef(const char* s, unsigned len)
    : str_(s), len_(len)
  { }
  // Warning -- lifetime of s must be longer than this stringref.
  explicit StringRef(const std::string& s)
    : str_(s.c_str()), len_(s.length())
  { }

  const char* c_str()  const { return str_; }

  size_t      size()   const { return len_; }
  const char* data()   const { return str_; }

  std::string str() const {
    if (!str_) return std::string();
    return std::string(str_, len_);
  }

  bool operator<(const StringRef& s) const {
    return strncmp(str_, s.str_, min(len_, s.len_)) < 0;
  }

  bool operator<=(const StringRef& s) const {
    return strncmp(str_, s.str_, min(len_, s.len_)) <= 0;
  }

  bool operator>(const StringRef& s) const {
    return strncmp(str_, s.str_, min(len_, s.len_)) > 0;
  }

  bool operator>=(const StringRef& s) const {
    return strncmp(str_, s.str_, min(len_, s.len_)) >= 0;
  }

  bool operator==(const StringRef& s) const {
    return (len_ == s.len_) &&
           strncmp(str_, s.str_, min(len_, s.len_)) == 0;
  }

  bool operator!=(const StringRef& s) const {
    return (len_ != s.len_) ||
           strncmp(str_, s.str_, min(len_, s.len_)) != 0;
  }

private:
  static unsigned min(unsigned x, unsigned y) {
    return (x <= y) ? x : y;
  }

  const char* str_;
  size_t      len_;
};



template<class T>
class ArrayRef {
public:
  typedef T* iterator;
  typedef const T* const_iterator;

  ArrayRef() : data_(nullptr), len_(0) { }
  ArrayRef(T* dat, size_t sz) : data_(dat), len_(sz) { }
  ArrayRef(T* begin, T* end)  : data_(begin), len_(end-begin) { }

  size_t size() const { return len_; }
  T& operator[](size_t i) { return data_[i]; }
  const T& operator[](size_t i) const { return data_[i]; }

  iterator begin() { return data_; }
  const_iterator begin() const { return begin(); }
  const_iterator cbegin() const { return begin(); }

  iterator end() { return data_ + len_; }
  const_iterator end() const { return end(); }
  const_iterator cend() const { return end(); }

private:
  T* data_;
  size_t len_;
};



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



// Copies s to mem, and returns a StringRef of mem.
// Mem must have space for at least s.length()+1 bytes of data.
inline StringRef copyStringRef(char* mem, StringRef s) {
  unsigned len = s.size();
  strncpy(mem, s.c_str(), len);
  mem[len] = 0;
  return StringRef(mem, len);
}


inline std::ostream& operator<<(std::ostream& ss, const StringRef str) {
  return ss.write(str.data(), str.size());
}

} // end namespace ohmu


// Must be included after the above.
#include "base/DiagnosticEmitter.h"


#endif  // OHMU_BASE_LLVM_DEPENDENCIES_H
