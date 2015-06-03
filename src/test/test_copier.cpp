//===- test_copier.cpp -----------------------------------------*- C++ --*-===//
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

#include "til/CFGBuilder.h"
#include "til/CopyReducer.h"
#include "til/TILPrettyPrint.h"
#include "til/TypedEvaluator.h"

#include <iostream>

using namespace ohmu;
using namespace til;

SExpr* makeSimple(CFGBuilder& bld) {
  auto* four = bld.newLiteralT<int>(4);
  four->addAnnotation(bld.newAnnotationT<SourceLocAnnot>(132));

  auto* vd = bld.newVarDecl(VarDecl::VK_Let, "four", four);
  vd->setVarIndex(1);
  auto* anncond = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(5),
      bld.newLiteralT<int>(3));
  anncond->addAnnotation(
      bld.newAnnotationT<PreconditionAnnot>(bld.newLiteralT<bool>(true)));
  vd->addAnnotation(bld.newAnnotationT<PreconditionAnnot>(anncond));

  auto* cond2    = bld.newLiteralT<int>(13);
  auto* precond2 = bld.newAnnotationT<PreconditionAnnot>(cond2);
  auto *cond     = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(6),
      bld.newLiteralT<int>(7));
  cond->addAnnotation(precond2);
  cond->addAnnotation(bld.newAnnotationT<InstrNameAnnot>("COMPARE"));

  auto* let = bld.newLet(vd, cond);
  let->addAnnotation(bld.newAnnotationT<InstrNameAnnot>("LET"));

  auto* A = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(200),
      bld.newLiteralT<int>(201));
  auto* B = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(300),
      bld.newLiteralT<int>(301));

  auto* Acond = bld.newLiteralT<int>(13);
  A->addAnnotation(bld.newAnnotationT<PreconditionAnnot>(Acond));
  B->addAnnotation(bld.newAnnotationT<InstrNameAnnot>("lequals"));

  auto* C = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(400),
      bld.newLiteralT<int>(401));
  auto* tri = bld.newAnnotationT<TestTripletAnnot>(A, B, C);
  let->addAnnotation(tri);

  return let;
}

void testCopying(CFGBuilder& bld, SExpr *e) {
  std::cout << "Original:\n";
  TILDebugPrinter::print(e, std::cout);
  std::cout << "\n\n";

  std::cout << "Copy to same arena:\n";
  SExprCopier copier(bld.arena());
  SExpr* e1 = copier.copy(e, bld.arena());
  TILDebugPrinter::print(e1, std::cout);
  std::cout << "\n\n";

  std::cout << "Copy to different arena:\n";
  MemRegion    region;
  MemRegionRef arena(&region);
  SExpr* e2 = copier.copy(e, arena);
  TILDebugPrinter::print(e2, std::cout);
  std::cout << "\n\n";

  std::cout << "Inplace reduce (TypedEvaluator):\n";
  TypedEvaluator eval(bld.arena());
  SExpr* e3 = eval.traverseAll(e);
  TILDebugPrinter::print(e3, std::cout);
  std::cout << "\n\n";
}


void testCopying() {
  MemRegion    region;
  MemRegionRef arena(&region);
  CFGBuilder   builder(arena);

  testCopying(builder, makeSimple(builder));
}

int main(int argc, const char** argv) {
  testCopying();
}
