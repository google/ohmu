//===- Annotation.h --------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_ANNOTATION_H
#define OHMU_TIL_ANNOTATION_H

#include "base/LLVMDependencies.h"
#include "base/MemRegion.h"

namespace ohmu {
namespace til {

/// Enum for the various kinds of attributes
enum TIL_AnnKind {
#define TIL_ANNKIND_DEF(X) ANNKIND_##X,
#include "TILAnnKinds.def"
};

/// Annotation stores one annotation and a next-pointer; thus doubling as a
/// linked list. New annotations need to be created in an arena.
class Annotation {
public:
  TIL_AnnKind kind() const { return static_cast<TIL_AnnKind>(Kind); }

  /// Allocate Annotation in the given region.  Annotations must be allocated in
  /// regions.
  void *operator new(size_t S, MemRegionRef &R) {
    return ::operator new(S, R);
  }

  /// Annotation objects cannot be deleted.
  // This declaration is public to workaround a gcc bug that breaks building
  // with REQUIRES_EH=1.
  void operator delete(void *) = delete;

  Annotation *next() const { return Next; }

  /// Insert annotation in this sorted list of annotations.
  void insert(Annotation *A) {
    if (A == nullptr)
      return;
    assert(A->kind() >= kind() && "Keep annotations sorted, change list head.");
    Annotation *Ap = this;
    while (Ap->next() && A->kind() >= Ap->kind())
      Ap = Ap->next();
    A->Next = Ap->next();
    Ap->Next = A;
  }

  /// Get annotation of the specified derived type. Returns nullptr if no such
  /// annotation exists in the list.
  template <class T>
  T *getAnnotation() {
    Annotation *Ap = this;
    do {
      if (isa<T>(Ap))
        return cast<T>(Ap);
      Ap = Ap->Next;
    } while (Ap);
    return nullptr;
  }

  /// Get all annotations of the specified derived type.
  template <class T>
  std::vector<T*> getAllAnnotations() {
    std::vector<T*> Res;
    T *A = getAnnotation<T>();
    // Using the fact that the list is sorted.
    while (A) {
      Res.push_back(A);
      if (A->next() && A->kind() == A->next()->kind())
        A = A->next();
      else
        A = nullptr;
    }
    return Res;
  }

protected:
  Annotation(TIL_AnnKind K) : Kind(K), Next(nullptr) { }

private:
  Annotation() = delete;

  /// Annotation objects must be created in an arena.
  void *operator new(size_t) = delete;

  const uint16_t Kind;

  Annotation* Next;
};

}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_ANNOTATION_H
