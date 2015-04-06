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

    std::cout << "\n";
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
  InMemoryReader(char* Buf, int Sz)
      : SourcePos(0), SourceSize(Sz), SourceBuffer(Buf)  {
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
    return static_cast<char*>( malloc(Sz + 1) );
  }

private:
  int totalLength() { return SourceSize - SourcePos; }

  int   SourcePos;
  int   SourceSize;
  char* SourceBuffer;
};


void testByteStream() {
  char* Buf = reinterpret_cast<char*>( malloc(1 << 16) );  // 64k buffer.
  int len = 0;

  {
    InMemoryWriter writer(Buf);

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

  InMemoryReader reader(Buf, len);

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
  free( const_cast<char*>(s.data()) );

  u8 = reader.readUInt8();
  CHECK(u8 == '-');

  s = reader.readString();
  CHECK(s == "World!");
  free( const_cast<char*>(s.data()) );

  int sign = 1;
  for (int i = 0; i < 5000; ++i) {
    i32 = reader.readInt32();
    CHECK(i32 == i*sign);
    sign = -sign;
  }

  s = reader.readString();
  CHECK(s == "Done.");
  free( const_cast<char*>(s.data()) );

  free(Buf);
}


SExpr* makeSimpleExpr(CFGBuilder& Builder) {
  auto *e1 = Builder.newLiteralT<int>(1);
  auto *e2 = Builder.newLiteralT<int>(2);
  auto *e3 = Builder.newBinaryOp(BOP_Add, e1, e2);
  e3->setBaseType(BaseType::getBaseType<int>());
  auto *e4 = Builder.newLiteralT<int>(3);
  auto *e5 = Builder.newBinaryOp(BOP_Mul, e3, e4);
  e5->setBaseType(BaseType::getBaseType<int>());
  auto *e6 = Builder.newUnaryOp(UOP_Negative, e5);
  return e6;
}


void testSerialization() {
  MemRegion    Region;
  MemRegionRef Arena(&Region);
  CFGBuilder   Builder(Arena);

  auto* e = makeSimpleExpr(Builder);
  TILDebugPrinter::print(e, std::cout);

  char* Buf = reinterpret_cast<char*>( malloc(1 << 16) );  // 64k buffer.
  int len = 0;

  InMemoryWriter writeStream(Buf);
  BytecodeWriter writer(&writeStream);

  writer.traverseAll(e);
  writeStream.flush();
  len = writeStream.totalLength();
  std::cout << "\n";
  std::cout << "Output " << len << " bytes.\n";
  writeStream.dump();

  InMemoryReader readStream(Buf, len);
  BytecodeReader reader(Builder, &readStream);
  auto* e2 = reader.read();

  if (e2)
    TILDebugPrinter::print(e2, std::cout);

  free(Buf);
}



int main(int argc, const char** argv) {
  testByteStream();
  testSerialization();
}

