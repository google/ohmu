//===- Util.h --------------------------------------------------*- C++ --*-===//
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
// This file defines various utility classes that are used by the rest of the
// language infrastructure.
//
//===----------------------------------------------------------------------===//


#ifndef OHMU_UTIL_H
#define OHMU_UTIL_H

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <unordered_map>


// A few handy definitions borrowed from LLVM
#ifndef LLVM_DELETED_FUNCTION
#define LLVM_DELETED_FUNCTION = delete
#endif


namespace ohmu {


template <class T, class U>
inline bool isa(const U* p) { return T::classof(p); }

template <class T, class U>
inline T* cast(U* p) {
  assert(T::classof(p));
  return static_cast<T*>(p);
}

template <class T, class U>
inline T& cast(U& p) {
  assert(T::classof(&p));
  return *static_cast<T*>(&p);
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
    : str_(s), len_((unsigned) strlen(s))
  { }
  StringRef(const char* s, unsigned len)
    : str_(s), len_(len)
  { }
  // Warning -- lifetime of s must be longer than this stringref.
  explicit StringRef(const std::string& s)
    : str_(s.c_str()), len_(s.length())
  { }

  size_t      length() const { return len_; }
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




// Copies s to mem, and returns a StringRef of mem.
// Mem must have space for at least s.length()+1 bytes of data.
inline StringRef copyStringRef(char* mem, StringRef s) {
  unsigned len = s.length();
  strncpy(mem, s.c_str(), len);
  mem[len] = 0;
  return StringRef(mem, len);
}


inline std::ostream& operator<<(std::ostream& ss, const StringRef str) {
  ss << str.c_str();
  return ss;
}


class PointerHash {
public:
  unsigned operator()(void* ptr) const {
    // Based on murmer hash
    const unsigned int m = 0x5bd1e995;
    unsigned i = *reinterpret_cast<unsigned*>(&ptr);
    return (((i*m) ^ (i >> 2))*m ^ (i >> 24))*m;
  }
};


template <class K, class T>
class HashMap {
private:
  typedef std::unordered_map<K, T> MapType;

public:
  size_t size() const { return hashmap_.size(); }

  void insert(const K& k, const T& t) {
    hashmap_.emplace(std::make_pair(k, t));
  }

  T find(const K& k, const T& invalid) {
    typename MapType::iterator it = hashmap_.find(k);
    if (it == hashmap_.end())
      return invalid;
    return (*it).second;
  }

private:
  MapType hashmap_;
};


}  // end namespace ohmu


#endif  //OHMU_UTIL_H
