//===- MutArrayRef.h -------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_BASE_MUTARRAYREF_H
#define OHMU_BASE_MUTARRAYREF_H


namespace ohmu {

// A version of ArrayRef which provides mutable access to the underlying data.
template<class T>
class MutArrayRef {
public:
  typedef T* iterator;
  typedef const T* const_iterator;

  MutArrayRef() : data_(nullptr), len_(0) { }
  MutArrayRef(T* dat, size_t sz) : data_(dat), len_(sz) { }
  MutArrayRef(T* begin, T* end)  : data_(begin), len_(end-begin) { }

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

}  // end namespace ohmu

#endif  // OHMU_BASE_MUTARRAYREF_H
