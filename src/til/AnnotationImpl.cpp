//===- AnnotationImpl.cpp --------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#include "AnnotationImpl.h"
#include "Bytecode.h"
#include "CFGBuilder.h"

namespace ohmu {
namespace til {


InstrNameAnnot *InstrNameAnnot::copy(CFGBuilder &Builder,
                                     const std::vector<SExpr*> &SubExprs) {
  return Builder.newAnnotationT<InstrNameAnnot>(Name);
}

void InstrNameAnnot::serialize(BytecodeWriter *B) {
  B->getWriter()->writeString(Name);
}

InstrNameAnnot *InstrNameAnnot::deserialize(BytecodeReader *B) {
  StringRef Nm = B->getReader()->readString();
  return B->getBuilder().newAnnotationT<InstrNameAnnot>(Nm);
}


SourceLocAnnot *SourceLocAnnot::copy(CFGBuilder &Builder,
                                     const std::vector<SExpr*> &SubExprs) {
  return Builder.newAnnotationT<SourceLocAnnot>(Position);
}

void SourceLocAnnot::serialize(BytecodeWriter *B) {
  B->getWriter()->writeInt64(Position);
}

SourceLocAnnot *SourceLocAnnot::deserialize(BytecodeReader *B) {
  SourcePosition P = B->getReader()->readInt64();
  return B->getBuilder().newAnnotationT<SourceLocAnnot>(P);
}


PreconditionAnnot*
PreconditionAnnot::copy(CFGBuilder &Builder,
                        const std::vector<SExpr*> &SubExprs) {
  return Builder.newAnnotationT<PreconditionAnnot>(SubExprs.at(0));
}

void PreconditionAnnot::serialize(BytecodeWriter *B) { }

PreconditionAnnot *PreconditionAnnot::deserialize(BytecodeReader *B) {
  PreconditionAnnot *A =
      B->getBuilder().newAnnotationT<PreconditionAnnot>(B->arg(0));
  B->drop(1);
  return A;
}


TestTripletAnnot *TestTripletAnnot::copy(CFGBuilder &Builder,
    const std::vector<SExpr*> &SubExprs) {
  return Builder.newAnnotationT<TestTripletAnnot>(
      SubExprs.at(0), SubExprs.at(1), SubExprs.at(2));
}

void TestTripletAnnot::serialize(BytecodeWriter *B) { }

TestTripletAnnot *TestTripletAnnot::deserialize(BytecodeReader *B) {
  TestTripletAnnot *A = B->getBuilder().newAnnotationT<TestTripletAnnot>(
      B->arg(2), B->arg(1), B->arg(0));
  B->drop(3);
  return A;
}


}  // end namespace til
}  // end namespace ohmu
