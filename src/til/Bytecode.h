//===- Bytecode.h ----------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_BYTECODE_H
#define OHMU_TIL_BYTECODE_H

#include "TIL.h"
#include "TILTraverse.h"


namespace ohmu {
namespace til {

/// Base class for bytecode readers and writers.
/// Common information about opcodes and bit widths are stored here.
class BytecodeBase {
public:
  // Maximum size of a single record (e.g. AST node).
  static const int MaxRecordSize = (1 << 12);  // 4k

  enum PsuedoOpcode : uint8_t {
    PSOP_Null = 0,
    PSOP_WeakInstrRef,
    PSOP_NullptrLiteral,
    PSOP_LastOp
  };

  void getBitSize(uint32_t) { }  // trigger an error for unhandled types.

  unsigned getBitSizeImpl(PsuedoOpcode)     { return 6; }
  unsigned getBitSizeImpl(TIL_Opcode)       { return 6; }
  unsigned getBitSizeImpl(TIL_UnaryOpcode)  { return 6; }
  unsigned getBitSizeImpl(TIL_BinaryOpcode) { return 6; }
  unsigned getBitSizeImpl(TIL_CastOpcode)   { return 6; }

  unsigned getBitSizeImpl(VarDecl::VariableKind)   { return 2; }
  unsigned getBitSizeImpl(Code::CallingConvention) { return 4; }
  unsigned getBitSizeImpl(Apply::ApplyKind)        { return 2; }
  unsigned getBitSizeImpl(Alloc::AllocKind)        { return 2; }

  template<class T>
  unsigned getBitSize() { return getBitSizeImpl(static_cast<T>(0)); }
};



/// Abstract base class for an output stream of bytes.
/// Derived classes must implement writeData to write the binary data to
/// a destination.  (E.g. file, network, etc.)
class ByteStreamWriterBase {
public:
  ByteStreamWriterBase() : Pos(0) { }

  virtual ~ByteStreamWriterBase() {
    assert(Pos == 0 && "Must flush writer before destruction.");
  }

  /// Write a block of data to disk.
  virtual void writeData(const void *Buf, int64_t Size) = 0;

  /// Flush buffer to disk.
  /// Derived classes should call this method in the destructor.
  void flush();

  /// Mark the end of a record.
  void endRecord();

  /// Emit a block of bytes.
  void writeBytes(const void *Data, int64_t Size);

  /// Emit up to 32 bits in little-endian byte order.
  void writeBits32(uint32_t V, int Nbits);

  /// Emit up to 64 bits in little-endian byte order.
  void writeBits64(uint64_t V, int Nbits);

  /// Emit a 32-bit unsigned int in a variable number of bytes.
  void writeUInt32_Vbr(uint32_t V);

  /// Emit a 64-bit unsigned int in a variable number of bytes.
  void writeUInt64_Vbr(uint64_t V);

  void writeBool(bool V) { writeBits32(V, 1); }

  void writeUInt8(uint8_t  V)  { writeBits32(V, 8);  }
  void writeUInt16(uint16_t V) { writeUInt32_Vbr(V); }
  void writeUInt32(uint32_t V) { writeUInt32_Vbr(V); }
  void writeUInt64(uint64_t V) { writeUInt64_Vbr(V); }

  void writeInt8(int8_t  V)  { writeBits32(static_cast<uint8_t> (V), 8);  }
  void writeInt16(int16_t V) { writeBits32(static_cast<uint16_t>(V), 16); }
  void writeInt32(int32_t V) { writeBits32(static_cast<uint32_t>(V), 32); }
  void writeInt64(int64_t V) { writeBits64(static_cast<uint64_t>(V), 64); }

  void writeFloat(float f);
  void writeDouble(double d);
  void writeString(StringRef S);

private:
  /// Returns the remaining size in the buffer
  inline int length() { return BufferSize - Pos; }

  /// Size of the buffer.  Default is 64k.
  static const int BufferSize = BytecodeBase::MaxRecordSize << 4;

  int Pos;
  uint8_t Buffer[BufferSize];
};



/// Abstract base class for an input stream of bytes.
/// Derived classes must implement readData to read the binary data from
/// a source.  (E.g. file, network, etc.)
class ByteStreamReaderBase {
public:
  ByteStreamReaderBase() : BufferLen(0), Pos(0), Eof(false) { }

  virtual ~ByteStreamReaderBase() { }

  /// Refill the buffer by reading from disk.
  /// Derived classes should call this method in the constructor.
  void refill();

  /// Read a block of data from disk.
  /// Returns the amount of data read, in bytes.
  /// If the amount is less than Size, we assume end of file.
  virtual int64_t readData(void *Buf, int64_t Size) = 0;

  /// Allocate memory for a new string.
  virtual char* allocStringData(uint32_t Size) = 0;

  /// Finish reading the current record.
  void endRecord();

  /// Read an interpreted blob of bytes.
  void readBytes(void *Data, int64_t Size);

  /// Read up to 32 bits, and return them as an unsigned int.
  uint32_t readBits32(int Nbits);

  /// Read up to 64 bits, and return them as an unsigned int.
  uint64_t readBits64(int Nbits);

  /// Read a 32-bit unsigned int in a variable number of bytes.
  uint32_t readUInt32_Vbr();

  /// Read a 64-bit unsigned int in a variable number of bytes.
  uint64_t readUInt64_Vbr();

  bool     readBool()   { return readBits32(1); }

  uint8_t  readUInt8()  { return static_cast<uint8_t> ( readBits32(8) ); }
  uint16_t readUInt16() { return static_cast<uint16_t>( readUInt32_Vbr() ); }
  uint32_t readUInt32() { return readUInt32_Vbr(); }
  uint64_t readUInt64() { return readUInt64_Vbr(); }

  int8_t   readInt8()   { return static_cast<int8_t> (readBits32(8));  }
  int16_t  readInt16()  { return static_cast<int16_t>(readBits32(16)); }
  int32_t  readInt32()  { return static_cast<int32_t>(readBits32(32)); }
  int64_t  readInt64()  { return static_cast<int64_t>(readBits64(64)); }

  float  readFloat();
  double readDouble();

  StringRef readString();

private:
  /// Return the remaining data in the buffer.
  int length() { return BufferLen - Pos; }

  /// Return true if we've reached the end of the stream.
  bool eof()   { return Eof; }

  /// Size of the buffer.  Default is 64k.
  static const int BufferSize = BytecodeBase::MaxRecordSize << 4;

  int     BufferLen;
  int     Pos;
  bool    Eof;
  bool    Error;
  uint8_t Buffer[BufferSize];
};



class BytecodeWriter : public Traversal<BytecodeWriter>,
                       public DefaultScopeHandler,
                       public BytecodeBase {
protected:
  typedef Traversal<BytecodeWriter> SuperTv;

  template<class T>
  void writeFlag(T Flag) { Writer->writeBits32(Flag, getBitSize<T>()); }

  void writePseudoOpcode(PsuedoOpcode Op) { writeFlag(Op); }

  void writeOpcode(TIL_Opcode Op) {
    writeFlag(static_cast<TIL_Opcode>(static_cast<unsigned>(PSOP_LastOp) + Op));
  }

  void writeBaseType(BaseType Bt) {
    Writer->writeUInt8(Bt.asUInt8());
    if (Bt.VectSize >= 1)
      Writer->writeBits32(Bt.VectSize, 4);
  }

  void writeLiteral(bool V)     { Writer->writeBool(V); }

  void writeLiteral(uint8_t  V) { Writer->writeUInt8(V);  }
  void writeLiteral(uint16_t V) { Writer->writeUInt16(V); }
  void writeLiteral(uint32_t V) { Writer->writeUInt32(V); }
  void writeLiteral(uint64_t V) { Writer->writeUInt64(V); }

  void writeLiteral(int8_t  V)  { Writer->writeInt8(V);  }
  void writeLiteral(int16_t V)  { Writer->writeInt16(V); }
  void writeLiteral(int32_t V)  { Writer->writeInt32(V); }
  void writeLiteral(int64_t V)  { Writer->writeInt64(V); }

  void writeLiteral(float  V)   { Writer->writeFloat(V);  }
  void writeLiteral(double V)   { Writer->writeDouble(V); }

  void writeLiteral(StringRef S) { Writer->writeString(S); }

  void writeLiteral(void* P) {
    if (P == nullptr)
      writePseudoOpcode(PSOP_NullptrLiteral);
    else
      writeOpcode(COP_Undefined);
  }

public:
  template<class T>
  void traverse(T *E, TraversalKind K) {
    SuperTv::traverse(E, K);
    Writer->endRecord();
  }

  template<class Ty>
  void reduceLiteralT(LiteralT<Ty>* E) {
    writeLiteral( E->value() );
  }

  void reduceWeak(Instruction *I);
  void reduceNull();
  void reduceVarDecl(VarDecl* E);
  void reduceFunction(Function *E);
  void reduceCode(Code *E) ;
  void reduceField(Field *E);
  void reduceSlot(Slot *E);
  void reduceRecord(Record *E);
  void reduceScalarType(ScalarType *E);
  void reduceSCFG(SCFG *E);
  void reduceBasicBlock(BasicBlock *E);
  void reduceLiteral(Literal *E);
  void reduceVariable(Variable *E);
  void reduceApply(Apply *E);
  void reduceProject(Project *E);
  void reduceCall(Call *E);
  void reduceAlloc(Alloc *E);
  void reduceLoad(Load *E);
  void reduceStore(Store *E);
  void reduceArrayIndex(ArrayIndex *E);
  void reduceArrayAdd(ArrayAdd *E);
  void reduceUnaryOp(UnaryOp *E);
  void reduceBinaryOp(BinaryOp *E);
  void reduceCast(Cast *E);
  void reducePhi(Phi *E);
  void reduceGoto(Goto *E);
  void reduceBranch(Branch *E);
  void reduceReturn(Return *E);
  void reduceUndefined(Undefined *E);
  void reduceWildcard(Wildcard *E);
  void reduceIdentifier(Identifier *E);
  void reduceLet(Let *E);
  void reduceIfThenElse(IfThenElse *E);

  BytecodeWriter(ByteStreamWriterBase *W) : Writer(W) { }

private:
  ByteStreamWriterBase *Writer;
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_BYTECODE_H
