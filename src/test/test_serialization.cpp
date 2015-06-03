//===- test_base.cpp -------------------------------------------*- C++ --*-===//
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



#include "til/Bytecode.h"
#include "til/CFGBuilder.h"
#include "til/TILPrettyPrint.h"


#include <memory>
#include <iostream>

using namespace ohmu;
using namespace til;


#define CHECK(B)                            \
  {                                         \
    bool b_check = B;                       \
    assert((b_check) && (#B " failed."));   \
    if (!(b_check))                         \
      exit(-1);                             \
  }

/// Simple writer that serializes to memory.
class InMemoryWriter : public ByteStreamWriterBase {
public:
  InMemoryWriter(char* Buf) :  TargetPos(0), TargetBuffer(Buf) { }
  virtual ~InMemoryWriter() { flush(); }

  /// Write a block of data to disk.
  virtual void writeData(const void *Buf, int64_t Size) override {
    memcpy(TargetBuffer + TargetPos, Buf, Size);
    TargetPos += Size;
  }

  int totalLength() { return TargetPos; }

  void dump() {
    char* Buf = TargetBuffer;
    int   Sz  = TargetPos;

    // bool prevChar = true;
    for (int64_t i = 0; i < Sz; ++i) {
      /*
      if (Buf[i] >= '0' && Buf[i] <= 'z') {
        if (!prevChar)
          std::cout << " ";
        std::cout << Buf[i];
        prevChar = true;
        continue;
      }
      */
      unsigned val = static_cast<uint8_t>(Buf[i]);
      std::cout << " " << val;
      // prevChar = false;
    }
    std::cout << "\n";
  }

private:
  int   TargetPos;
  char* TargetBuffer;
};


/// Simple reader that serializes from memory.
class InMemoryReader : public ByteStreamReaderBase {
public:
  InMemoryReader(char* Buf, int Sz, MemRegionRef A)
      : SourcePos(0), SourceSize(Sz), SourceBuffer(Buf), Arena(A)  {
    refill();
  }

  /// Write a block of data to disk.
  virtual int64_t readData(void *Buf, int64_t Sz) override {
    if (Sz > totalLength())
      Sz = totalLength();
    memcpy(Buf, SourceBuffer + SourcePos, Sz);
    SourcePos += Sz;
    return Sz;
  }

  virtual char* allocStringData(uint32_t Sz) override {
    return Arena.allocateT<char>(Sz + 1);
  }

private:
  int totalLength() { return SourceSize - SourcePos; }

  int   SourcePos;
  int   SourceSize;
  char* SourceBuffer;
  MemRegionRef Arena;
};


void testByteStream() {
  MemRegion    region;
  MemRegionRef arena(&region);

  char* buf = arena.allocateT<char>(1 << 16);  // 64k buffer.
  int len = 0;

  {
    InMemoryWriter writer(buf);

    writer.writeBool(true);
    writer.writeBool(false);

    writer.writeUInt8('A');
    writer.writeUInt16(12345);
    writer.writeUInt32(1234567890);
    writer.writeUInt64(12345678900000);

    writer.writeInt8(-52);
    writer.writeInt16(-12345);
    writer.writeInt32(-1234567890);
    writer.writeInt64(-12345678900000);

    writer.writeFloat(12.3f);
    writer.writeDouble(23.4);
    writer.writeString("Hello ");
    writer.writeUInt8('-');
    writer.writeString("World!");

    int sign = 1;
    for (unsigned i = 0; i < 5000; ++i) {
      writer.writeInt32(i*sign);
      sign = -sign;
    }

    writer.writeString("Done.");
    writer.flush();
    len = writer.totalLength();

    // writer.dump();
  }

  InMemoryReader reader(buf, len, arena);

  bool b = reader.readBool();
  CHECK(b == true);

  b = reader.readBool();
  CHECK(b == false);


  uint8_t u8 = reader.readUInt8();
  CHECK(u8 == 'A');

  uint16_t u16 = reader.readUInt16();
  CHECK(u16 == 12345);

  uint32_t u32 = reader.readUInt32();
  CHECK(u32 == 1234567890);

  uint64_t u64 = reader.readUInt64();
  CHECK(u64 == 12345678900000);


  int8_t i8 = reader.readInt8();
  CHECK(i8 == -52);

  int16_t i16 = reader.readInt16();
  CHECK(i16 == -12345);

  int32_t i32 = reader.readInt32();
  CHECK(i32 == -1234567890);

  int64_t i64 = reader.readInt64();
  CHECK(i64 == -12345678900000);


  float f = reader.readFloat();
  CHECK(f == 12.3f);

  double d = reader.readDouble();
  CHECK(d == 23.4);

  StringRef s = reader.readString();
  CHECK(s == "Hello ");

  u8 = reader.readUInt8();
  CHECK(u8 == '-');

  s = reader.readString();
  CHECK(s == "World!");

  int sign = 1;
  for (int i = 0; i < 5000; ++i) {
    i32 = reader.readInt32();
    CHECK(i32 == i*sign);
    sign = -sign;
  }

  s = reader.readString();
  CHECK(s == "Done.");
}






SExpr* makeSimpleExpr(CFGBuilder& bld) {
  auto *e1 = bld.newLiteralT<int>(1);
  auto *e2 = bld.newLiteralT<int>(2);
  auto *e3 = bld.newBinaryOp(BOP_Add, e1, e2);
  e3->setBaseType(BaseType::getBaseType<int>());
  auto *e4 = bld.newLiteralT<int>(3);
  auto *e5 = bld.newBinaryOp(BOP_Mul, e3, e4);
  e5->setBaseType(BaseType::getBaseType<int>());
  auto *e6 = bld.newUnaryOp(UOP_Negative, e5);
  return e6;
}


SExpr* makeBranch(CFGBuilder& bld) {
  // declare self parameter for enclosing module
  auto *self_vd = bld.newVarDecl(VarDecl::VK_SFun, "self", nullptr);
  bld.enterScope(self_vd);
  auto *self = bld.newVariable(self_vd);

  // declare parameters for sum function
  auto *int_ty = bld.newScalarType(BaseType::getBaseType<int>());
  auto *vd_n = bld.newVarDecl(VarDecl::VK_Fun, "n", int_ty);
  bld.enterScope(vd_n);
  auto *n = bld.newVariable(vd_n);

  bld.beginCFG(nullptr);
  auto *cfg = bld.currentCFG();

  bld.beginBlock(cfg->entry());
  bld.newReturn(bld.newLiteralT<int>(0));

  auto *label1 = bld.newBlock(0);
  bld.beginBlock(label1);
  auto *cond = bld.newBinaryOp(BOP_Leq,
      bld.newLiteralT<int>(0), bld.newLiteralT<int>(0));
  cond->setBaseType(BaseType::getBaseType<bool>());

  // Annotations:
  auto *name = bld.newAnnotationT<InstrNameAnnot>("SomeNe");
  auto* sourcepos = bld.newAnnotationT<SourceLocAnnot>(10);
  auto* cond2 = bld.newLiteralT<bool>(true);
  auto* cond3 = bld.newLiteralT<bool>(false);
  cond2->addAnnotation(sourcepos);
  auto* precond2 = bld.newAnnotationT<PreconditionAnnot>(cond2);
  cond3->addAnnotation(precond2);
  auto* precond3 = bld.newAnnotationT<PreconditionAnnot>(cond3);
  cond->addAnnotation(precond3);
  cond->addAnnotation(name);

  bld.newBranch(cond, cfg->entry(), cfg->entry());

  bld.endCFG();

  // construct sum function
  auto *sum_c   = bld.newCode(int_ty, cfg);
  bld.exitScope();
  auto *sum_f   = bld.newFunction(vd_n, sum_c);
  auto *sum_slt = bld.newSlot("sum", sum_f);


  // build enclosing record
  auto *rec = bld.newRecord(2);
  rec->addSlot(bld.arena(), sum_slt);

  // build enclosing module
  bld.exitScope();
  auto *mod = bld.newFunction(self_vd, sum_slt);

  return mod;

}

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


void testSerialization(CFGBuilder& bld, char* buf, SExpr *e) {
  std::cout << "\n";
  TILDebugPrinter::print(e, std::cout);
  std::cout << "\n\n";

  int len = 0;

  InMemoryWriter writeStream(buf);
  BytecodeWriter writer(&writeStream);

  writer.traverseAll(e);
  writeStream.flush();
  len = writeStream.totalLength();
  std::cout << "Output " << len << " bytes.\n";
  writeStream.dump();
  std::cout << "\n";

  InMemoryReader readStream(buf, len, bld.arena());
  BytecodeReader reader(bld, &readStream);
  auto* e2 = reader.read();

  if (e2) {
    TILDebugPrinter::print(e2, std::cout);
    std::cout << "\n\n";
  }
}



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
  B->addAnnotation(bld.newAnnotationT<InstrNameAnnot>("google"));

  auto* C = bld.newBinaryOp(BOP_Leq, bld.newLiteralT<int>(400),
      bld.newLiteralT<int>(401));
  auto* tri = bld.newAnnotationT<TestTripletAnnot>(A, B, C);
  let->addAnnotation(tri);

  return let;
}


void testSerialization() {
  MemRegion    region;
  MemRegionRef arena(&region);
  CFGBuilder   builder(arena);
  char* buf = arena.allocateT<char>(1 << 16);  // 64k buffer

  testSerialization(builder, buf, makeBranch(builder));
  testSerialization(builder, buf, makeSimpleExpr(builder));
  testSerialization(builder, buf, makeModule(builder));
  testSerialization(builder, buf, makeSimple(builder));
}



int main(int argc, const char** argv) {
  testByteStream();
  testSerialization();
}

