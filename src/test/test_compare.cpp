//===- test_compare.cpp ----------------------------------------*- C++ --*-===//
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

#include "base/LLVMDependencies.h"

#include "test/Driver.h"

#include "til/CFGBuilder.h"
#include "til/CopyReducer.h"
#include "til/TILPrettyPrint.h"
#include "til/TILCompare.h"

#include <iostream>

using namespace ohmu;
using namespace til;

// Helpers

void addSourceLocAnn(CFGBuilder &bld, SExpr *E, int p) {
  auto *a = bld.newAnnotationT<SourceLocAnnot>(p);
  E->addAnnotation(a);
}

void addInstrNameAnn(CFGBuilder &bld, SExpr *E, StringRef Name) {
  auto *a = bld.newAnnotationT<InstrNameAnnot>(Name);
  E->addAnnotation(a);
}

void addPreconditionAnn(CFGBuilder &bld, SExpr *E, SExpr *Condition) {
  auto *a = bld.newAnnotationT<PreconditionAnnot>(Condition);
  E->addAnnotation(a);
}

SExpr *simpleComparison(CFGBuilder &bld, TIL_BinaryOpcode Op, int a, int b) {
  auto *alit = bld.newLiteralT<int>(a);
  auto *blit = bld.newLiteralT<int>(b);
  return bld.newBinaryOp(Op, alit, blit);
}

// Constructing expressions with annotations

SExpr *testNoAnn(CFGBuilder &bld) {
  SExpr *E = simpleComparison(bld, BOP_Leq, 2, 4);
  return E;
}

SExpr *testSingleLocAnn(CFGBuilder &bld) {
  SExpr *E = testNoAnn(bld);
  addSourceLocAnn(bld, E, 5);
  return E;
}

SExpr *testSingleLocAnnAlt(CFGBuilder &bld) {
  SExpr *E = testNoAnn(bld);
  addSourceLocAnn(bld, E, 4);
  return E;
}

SExpr *testDoubleLocAnn(CFGBuilder &bld) {
  SExpr *E = testSingleLocAnn(bld);
  addSourceLocAnn(bld, E, 5);
  return E;
}

SExpr *testSingleNameAnn(CFGBuilder &bld) {
  SExpr *E = testNoAnn(bld);
  addInstrNameAnn(bld, E, "TEST");
  return E;
}

SExpr *testNestedAnn(CFGBuilder &bld) {
  SExpr *E = testNoAnn(bld);
  SExpr *inner = simpleComparison(bld, BOP_Leq, 210, 30);
  addSourceLocAnn(bld, inner, 6);
  addPreconditionAnn(bld, E, inner);
  return E;
}

SExpr *testNestedAnnAlt(CFGBuilder &bld) {
  SExpr *E = testNoAnn(bld);
  SExpr *inner = simpleComparison(bld, BOP_Leq, 210, 33);
  addSourceLocAnn(bld, inner, 6);
  addPreconditionAnn(bld, E, inner);
  return E;
}

// Expressions that should not get TypeEvaluated (e.g. 1+2).

SExpr* envOutsideLet(CFGBuilder& bld) {
  auto* one = bld.newLiteralT<int>(1);
  auto* a = bld.newVarDecl(VarDecl::VK_Let, "a", one);
  auto* l = bld.newLet(a, bld.newVariable(a));
  return bld.newBinaryOp(BOP_Add, bld.newLiteralT<int>(2), l);
}

SExpr* envInsideLet(CFGBuilder& bld) {
  auto* one = bld.newLiteralT<int>(1);
  auto* a = bld.newVarDecl(VarDecl::VK_Let, "a", one);
  auto* s = bld.newBinaryOp(BOP_Add,
      bld.newLiteralT<int>(2), bld.newVariable(a));
  return bld.newLet(a, s);
}

SExpr *simpleSum(CFGBuilder &bld) {
  auto *s1 = simpleComparison(bld, BOP_Add, 1, 2);
  auto *s2 = simpleComparison(bld, BOP_Add, 1, 2);
  return bld.newBinaryOp(BOP_Add, s1, s2);
}

SExpr* simpleSumLet(CFGBuilder& bld) {
  auto* one = bld.newLiteralT<int>(1);
  auto* a = bld.newVarDecl(VarDecl::VK_Let, "a", one);
  bld.enterScope(a);

  auto* two = bld.newLiteralT<int>(2);
  auto* b = bld.newVarDecl(VarDecl::VK_Let, "b", two);
  bld.enterScope(b);

  auto* s1 = bld.newBinaryOp(BOP_Add, bld.newVariable(a), bld.newVariable(b));
  auto* c = bld.newVarDecl(VarDecl::VK_Let, "c", s1);
  bld.enterScope(c);

  auto* s2 = bld.newBinaryOp(BOP_Add, bld.newVariable(c), bld.newVariable(c));

  bld.exitScope(); // c
  auto* let_c = bld.newLet(c, s2);
  bld.exitScope(); // b
  auto* let_b = bld.newLet(b, let_c);
  bld.exitScope(); // a
  auto* let_a = bld.newLet(a, let_b);

  return let_a;
}

// Something large.

SExpr* makeModule(CFGBuilder& bld) {
  // declare self parameter for enclosing module
  auto *self_vd = bld.newVarDecl(VarDecl::VK_SFun, "self", nullptr);
  bld.enterScope(self_vd);
  auto *self = bld.newVariable(self_vd);

  // declare parameters for sum function
  auto *int_ty = bld.newScalarType(BaseType::getBaseType<int>());
  auto *vd_n = bld.newVarDecl(VarDecl::VK_Fun, "n", int_ty);
  bld.enterScope(vd_n);
  auto *n = bld.newVariable(vd_n);

  // construct CFG for sum function
  bld.beginCFG(nullptr);
  auto *cfg = bld.currentCFG();

  bld.beginBlock(cfg->entry());
  auto *i      = bld.newLiteralT<int>(0);
  auto *total  = bld.newLiteralT<int>(0);
  auto *jfld   = bld.newField(int_ty, i);
  auto *jptr   = bld.newAlloc(jfld, Alloc::AK_Local);
  auto *label1 = bld.newBlock(2);
  SExpr* args[2] = { i, total };
  bld.newGoto(label1, ArrayRef<SExpr*>(args, 2));

  bld.beginBlock(label1);
  auto *iphi     = bld.currentBB()->arguments()[0];
  auto *totalphi = bld.currentBB()->arguments()[1];
  auto *cond     = bld.newBinaryOp(BOP_Leq, iphi, n);
  cond->setBaseType(BaseType::getBaseType<bool>());
  auto *label2   = bld.newBlock();
  auto *label3   = bld.newBlock();
  bld.newBranch(cond, label2, label3);

  bld.beginBlock(label2);
  auto *i2  = bld.newBinaryOp(BOP_Add, iphi, bld.newLiteralT<int>(1));
  i2->setBaseType(BaseType::getBaseType<int>());
  auto *jld = bld.newLoad(jptr);
  jld->setBaseType(BaseType::getBaseType<int>());
  auto *j2  = bld.newBinaryOp(BOP_Add, jld, bld.newLiteralT<int>(1));
  j2->setBaseType(BaseType::getBaseType<int>());
  bld.newStore(jptr, j2);
  auto *total2    = bld.newBinaryOp(BOP_Add, totalphi, iphi);
  total2->setBaseType(BaseType::getBaseType<int>());
  SExpr* args2[2] = { i2, total2 };
  bld.newGoto(label1, ArrayRef<SExpr*>(args2, 2));

  bld.beginBlock(label3);
  bld.newGoto(cfg->exit(), total2);

  bld.endCFG();

  // construct sum function
  auto *sum_c   = bld.newCode(int_ty, cfg);
  bld.exitScope();
  auto *sum_f   = bld.newFunction(vd_n, sum_c);
  auto *sum_slt = bld.newSlot("sum", sum_f);

  // declare parameters for sum function
  auto *vd_m = bld.newVarDecl(VarDecl::VK_Fun, "m", int_ty);
  bld.enterScope(vd_m);
  auto *m = bld.newVariable(vd_m);

  auto *vd_tot = bld.newVarDecl(VarDecl::VK_Fun, "total", int_ty);
  bld.enterScope(vd_tot);
  auto *tot = bld.newVariable(vd_tot);

  auto *ifcond = bld.newBinaryOp(BOP_Eq, m, bld.newLiteralT<int>(0));
  ifcond->setBaseType(BaseType::getBaseType<int>());
  auto *zero   = bld.newLiteralT<int>(0);

  auto *m2   = bld.newBinaryOp(BOP_Sub, m, bld.newLiteralT<int>(1));
  m2->setBaseType(BaseType::getBaseType<int>());
  auto *tot2 = bld.newBinaryOp(BOP_Add, tot, m);
  tot2->setBaseType(BaseType::getBaseType<int>());
  auto *app1 = bld.newApply(self, nullptr, Apply::FAK_SApply);
  auto *app2 = bld.newProject(app1, "sum2");
  auto *app3 = bld.newApply(app2, m2);
  auto *app4 = bld.newApply(app3, tot2);
  auto *fcall = bld.newCall(app4);

  auto *ife = bld.newIfThenElse(ifcond, zero, fcall);
  auto *sum2_c   = bld.newCode(int_ty, ife);
  bld.exitScope();
  auto *sum2_f1  = bld.newFunction(vd_tot, sum2_c);
  bld.exitScope();
  auto *sum2_f2  = bld.newFunction(vd_m, sum2_f1);
  auto *sum2_slt = bld.newSlot("sum2", sum2_f2);

  // build enclosing record
  auto *rec = bld.newRecord(2);
  rec->addSlot(bld.arena(), sum_slt);
  rec->addSlot(bld.arena(), sum2_slt);

  // build enclosing module
  bld.exitScope();
  auto *mod = bld.newFunction(self_vd, rec);

  return mod;
}

///////
// Test 'framework'
///////

// Parse input into MemRegion provided in Global.
SExpr *simpleParse(Global &G, const char *inp) {
  Driver driver;
  bool success = driver.initParser("src/grammar/ohmu.grammar");
  assert(success && "Initializing ohmu grammer failed.");

  StringStream S(inp);
  success = driver.parseDefinitions(&G, S);
  if (!success) {
    std::cout << "Parsing input failed: " << inp << std::endl;
    return nullptr;
  }
  G.lower();
  return G.global();
}

int tests = 0;
int successTests = 0;
int failedTests = 0;

void testEquals(const SExpr *E1, const SExpr *E2, bool exp) {

  tests++;
  if (EqualsComparator::compareExprs(E1, E2) != exp) {
    std::cout << "Test failed, expected " << exp << "." << std::endl;
    std::cout << "Comparing" << std::endl;
    TILDebugPrinter::print(E1, std::cout);
    std::cout << std::endl << "with" << std::endl;
    TILDebugPrinter::print(E2, std::cout);
    std::cout << std::endl;
    failedTests++;
  } else {
    successTests++;
  }
}

void testEquals(const char *I1, const char *I2, bool exp) {
  // Parser does not yet support multiple calls.
  Global G1;
  SExpr *E1 = simpleParse(G1, I1);
  Global G2;
  SExpr *E2 = simpleParse(G2, I2);

  if (!E1 || !E2)
    return;

  testEquals(E1, E2, exp);
}

void testCompare() {
  MemRegion    region;
  MemRegionRef arena(&region);
  CFGBuilder   builder(arena);

  // Basic.
  testEquals("x=1;","x=1;", true);
  testEquals("x=1;","x=2;", false);
  testEquals("f(a:Int):Int->(a);","f(a:Int):Int->(a);", true);

  // Variable renaming.
  testEquals("x={let a=3; a;};","x={let b=3; b;};", true);
  testEquals("x={let a=3; a;};","x={let b=4; b;};", false);
  testEquals("f(a:Int):Int->(a);","f(b:Int):Int->(b);", true);
  testEquals("f(a:Int):Int->(a);","f(b:Int):Int->(3);", false);

  // Let unrolling.
  testEquals("x=16;","x={let y=16; y;};", true);
  testEquals("x=16;","x={let y=17; y;};", false);
  testEquals("x={let a=1; let b=2; a+b;};","x={let y=2; let x=1; x+y;};", true);
  // Need to specified directly due to typedEvaluator replacing (1+2)+(1+2)
  // with 6.
  testEquals(simpleSum(builder), simpleSumLet(builder), true);
  testEquals(envOutsideLet(builder), envInsideLet(builder), true);

  // Annotations.

  testEquals(testNoAnn(builder), testNoAnn(builder), true);
  testEquals(testNoAnn(builder), testSingleLocAnn(builder), false);
  testEquals(testSingleLocAnn(builder), testSingleLocAnn(builder), true);
  testEquals(testSingleLocAnn(builder), testSingleNameAnn(builder), false);
  testEquals(testSingleLocAnn(builder), testSingleLocAnnAlt(builder), false);
  testEquals(testSingleLocAnn(builder), testDoubleLocAnn(builder), false);
  testEquals(testDoubleLocAnn(builder), testDoubleLocAnn(builder), true);
  testEquals(testSingleNameAnn(builder), testSingleNameAnn(builder), true);
  testEquals(testNestedAnn(builder), testNestedAnn(builder), true);
  testEquals(testNestedAnn(builder), testNestedAnnAlt(builder), false);

  // Testing larger AST.
  testEquals(makeModule(builder), makeModule(builder), true);

  std::cout << "Ran " << tests << " tests. ";
  std::cout << failedTests << " failed, ";
  std::cout << (tests - successTests - failedTests) << " aborted." << std::endl;
}

int main(int argc, const char** argv) {
  testCompare();
}
