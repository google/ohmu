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
  InMemoryWriter(char* Buf) :  Pos(0), Buffer(Buf) { }
  virtual ~InMemoryWriter() { flush(); }

  /// Write a block of data to disk.
  virtual void writeData(const void *Buf, int64_t Size) override {
    memcpy(Buffer + Pos, Buf, Size);
    Pos += Size;
  }

  int length() { return Pos; }

  void dump() {
    char* Buf  = Buffer;
    int   Size = Pos;

    std::cout << "\n";
    bool prevChar = true;
    for (int64_t i = 0; i < Size; ++i) {
      if (Buf[i] >= '0' && Buf[i] <= 'z') {
        if (!prevChar)
          std::cout << " ";
        std::cout << Buf[i];
        prevChar = true;
        continue;
      }
      unsigned val = static_cast<uint8_t>(Buf[i]);
      std::cout << " " << val;
      prevChar = false;
    }
    std::cout << "\n";
  }

private:
  int Pos;
  char* Buffer;
};


/// Simple reader that serializes from memory.
class InMemoryReader : public ByteStreamReaderBase {
public:
  InMemoryReader(char* Buf, int Sz) : Pos(0), Size(Sz), Buffer(Buf)  {
    refill();
  }

  /// Write a block of data to disk.
  virtual int64_t readData(void *Buf, int64_t Sz) override {
    if (Sz > length())
      Sz = length();
    memcpy(Buf, Buffer + Pos, Sz);
    Pos += Sz;
    return Sz;
  }

  virtual char* allocStringData(uint32_t Sz) override {
    return static_cast<char*>( malloc(Sz + 1) );
  }

private:
  int length() { return Size - Pos; }

  int Pos;
  int Size;
  char* Buffer;
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
    len = writer.length();

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


int main(int argc, const char** argv) {
  testByteStream();
}

