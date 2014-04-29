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

namespace ohmu {


template <class T, class U>
inline bool isa(const U* p) { return T::classof(p); }

template <class T, class U>
inline T* cast(U* p) {
  assert(T::classof(p));
  return reinterpret_cast<T*>(p);
}

template <class T, class U>
inline const T* cast(const U* p) {
  assert(T::classof(p));
  return reinterpret_cast<T*>(p);
}

template <class T, class U>
inline T* dyn_cast(U* p) {
  if (!T::classof(p))
    return 0;
  return reinterpret_cast<T*>(p);
}

template <class T, class U>
inline const T* dyn_cast(const U* p) {
  if (!T::classof(p))
    return 0;
  return reinterpret_cast<T*>(p);
}

template <class T, class U>
inline T* dyn_cast_or_null(U* p) {
  if (!p || !T::classof(p))
    return 0;
  return reinterpret_cast<T*>(p);
}

template <class T, class U>
inline const T* dyn_cast_or_null(const U* p) {
  if (!p || !T::classof(p))
    return 0;
  return reinterpret_cast<T*>(p);
}



class StringRef {
public:
  StringRef(const char* s, unsigned len)
    : str_(s), len_(len)
  { }
  explicit StringRef(const char* s)
    : str_(s), len_((unsigned) strlen(s))
  { }
  // Warning -- lifetime of s must be longer than this stringref.
  explicit StringRef(const std::string& s)
    : str_(s.c_str()), len_(s.length())
  { }

  unsigned    length() const { return len_; }
  const char* c_str()  const { return str_; }

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
  unsigned    len_;
};


// Copies s to mem, and returns a StringRef of mem.
// Mem must have space for at least s.length()+1 bytes of data.
inline StringRef copyStringRef(char* mem, StringRef s) {
  unsigned len = s.length();
  strncpy(mem, s.c_str(), len);
  mem[len] = 0;
  return StringRef(mem, len);
}


std::ostream& operator<<(std::ostream& ss, const StringRef& str) {
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


}  // end namespace ohmu


#endif  //OHMU_UTIL_H
