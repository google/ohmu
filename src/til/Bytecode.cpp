//===- Serialize.cpp -------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//


#include "Bytecode.h"

namespace ohmu {
namespace til {


void ByteStreamWriterBase::flush() {
  if (Pos > 0)
    writeData(Buffer, Pos);
  Pos = 0;
}


void ByteStreamReaderBase::refill() {
  if (eof())
    return;

  if (Pos > 0) {  // Move remaining contents to start of buffer.
    assert(Pos > length() && "Cannot refill a nearly full buffer.");

    int len = length();
    if (len > 0)
      memcpy(Buffer, Buffer + Pos, len);
    Pos = 0;
    BufferLen = len;
  }

  int read = readData(Buffer + BufferLen, BufferSize - BufferLen);
  BufferLen += read;
  if (BufferLen < BufferSize)
    Eof = true;
}


void ByteStreamWriterBase::endRecord() {
  if (length() <= BytecodeBase::MaxRecordSize)
    flush();
}


void ByteStreamReaderBase::endRecord() {
  if (length() <= BytecodeBase::MaxRecordSize)
    refill();
}


void ByteStreamWriterBase::writeBytes(const void *Data, int64_t Size) {
  if (Size >= (BufferSize >> 1)) {   // Don't buffer large writes.
    flush();                  // Flush current data to disk.
    writeData(Data, Size);    // Directly write the bytes to disk.
    return;
  }
  // Flush buffer if the write would fill it up.
  if (length() - Size <= BytecodeBase::MaxRecordSize)
    flush();

  memcpy(Buffer + Pos, Data, Size);
  Pos += Size;
  // Size < BufferSize/2, so we have at least half the buffer left.
}


void ByteStreamReaderBase::readBytes(void *Data, int64_t Size) {
  int len = length();
  if (Size > len) {
    memcpy(Data, Buffer + Pos, len);   // Copy out current buffer.
    Size = Size - len;
    Data = reinterpret_cast<char*>(Data) + len;

    if (Size >= (BufferSize >> 1)) {   // Don't buffer large reads.
      if (eof()) {
        Error = true;
        return;
      }
      int64_t L = readData(Data, Size);   // Read more data.
      if (L < Size)
        Eof = true;
      refill();                        // Refill buffer
      return;
    }

    refill();
    if (Size > length()) {
      Error = true;
      return;
    }
  }

  // Size < length() at this point.
  memcpy(Data, Buffer + Pos, Size);
  Pos += Size;
  if (length() < BytecodeBase::MaxRecordSize)
    refill();
}


void ByteStreamWriterBase::writeBits32(uint32_t V, int Nbits) {
  while (Nbits > 0) {
    Buffer[Pos++] = V & 0xFF;
    V = V >> 8;
    Nbits -= 8;
  }
}


void ByteStreamWriterBase::writeBits64(uint64_t V, int Nbits) {
  while (Nbits > 0) {
    Buffer[Pos++] = V & 0xFF;
    V = V >> 8;
    Nbits -= 8;
  }
}


uint32_t ByteStreamReaderBase::readBits32(int Nbits) {
  assert(Nbits <= 32 && "Invalid number of bits.");
  uint32_t V = 0;
  int B = 0;
  while (true) {
    uint32_t Byt = Buffer[Pos++];
    V = V | (Byt << B);
    B += 8;
    if (B >= Nbits)
      break;
  }
  return V;
}


uint64_t ByteStreamReaderBase::readBits64(int Nbits) {
  assert(Nbits <= 64 && "Invalid number of bits.");
  uint64_t V = 0;
  int B = 0;
  while (true) {
    uint64_t Byt = Buffer[Pos++];
    V = V | (Byt << B);
    B += 8;
    if (B >= Nbits)
      break;
  }
  return V;
}


void ByteStreamWriterBase::writeUInt32_Vbr(uint32_t V) {
  if (V == 0) {
    Buffer[Pos++] = 0;
    return;
  }
  while (V > 0) {
    uint32_t V2 = V >> 7;
    uint8_t Hibit = (V2 == 0) ? 0 : 0x80;
    // Write lower 7 bits.  The 8th bit is high if there's more to write.
    Buffer[Pos++] = static_cast<uint8_t>((V & 0x7F) | Hibit);
    V = V2;
  }
}


void ByteStreamWriterBase::writeUInt64_Vbr(uint64_t V) {
  if (V == 0) {
    Buffer[Pos++] = 0;
    return;
  }
  while (V > 0) {
    uint64_t V2 = V >> 7;
    uint8_t Hibit = (V2 == 0) ? 0 : 0x80;
    // Write lower 7 bits.  The 8th bit is high if there's more to write.
    Buffer[Pos++] = static_cast<uint8_t>((V & 0x7Fu) | Hibit);
    V = V2;
  }
}


uint32_t ByteStreamReaderBase::readUInt32_Vbr() {
  uint32_t V = 0;
  for (unsigned B = 0; B < 32; B += 7) {
    uint32_t Byt = Buffer[Pos++];
    V = V | ((Byt & 0x7Fu) << B);
    if ((Byt & 0x80) == 0)
      break;
  }
  return V;
}


uint64_t ByteStreamReaderBase::readUInt64_Vbr() {
  uint64_t V = 0;
  for (unsigned B = 0; B < 64; B += 7) {
    uint64_t Byt = Buffer[Pos++];
    V = V | ((Byt & 0x7Fu) << B);
    if ((Byt & 0x80) == 0)
      break;
  }
  return V;
}


void ByteStreamWriterBase::writeFloat(float f) {
  // TODO: works only on machines which use in-memory IEEE format.
  union { float Fnum; uint32_t Inum; } U;
  U.Fnum = f;
  writeUInt32(U.Inum);
}


void ByteStreamWriterBase::writeDouble(double d) {
  // TODO: works only on machines which use in-memory IEEE format.
  union { double Fnum; uint64_t Inum; } U;
  U.Fnum = d;
  writeUInt64(U.Inum);
}


float ByteStreamReaderBase::readFloat() {
  // TODO: works only on machines which use in-memory IEEE format.
  union { float Fnum; uint32_t Inum; } U;
  U.Inum = readUInt32();
  return U.Fnum;
}


double ByteStreamReaderBase::readDouble() {
  // TODO: works only on machines which use in-memory IEEE format.
  union { double Fnum; uint64_t Inum; } U;
  U.Inum = readUInt64();
  return U.Fnum;
}


void ByteStreamWriterBase::writeString(StringRef S) {
  writeUInt32(S.size());
  writeBytes(S.data(), S.size());
}


StringRef ByteStreamReaderBase::readString() {
  uint32_t Sz = readUInt32();
  char* S = allocStringData(Sz);
  if (!S) {
    Error = true;
    return StringRef(nullptr, 0);
  }
  readBytes(S, Sz);
  return StringRef(S, Sz);
}


/** BytecodeWriter and BytecodeReader **/


void BytecodeWriter::reduceWeak(Instruction *I) {
  writePseudoOpcode(PSOP_WeakInstrRef);
  Writer->writeUInt32(I->instrID());
}


void BytecodeWriter::reduceNull() {
  writePseudoOpcode(PSOP_Null);
}


void BytecodeWriter::reduceVarDecl(VarDecl* E) {
  writeOpcode(COP_VarDecl);
  writeFlag(E->kind());
  Writer->writeUInt32(E->varIndex());
}


void BytecodeWriter::reduceFunction(Function *E) {
  writeOpcode(COP_Function);
}


void BytecodeWriter::reduceCode(Code *E) {
  writeOpcode(COP_Code);
  writeFlag(E->callingConvention());
}


void BytecodeWriter::reduceField(Field *E) {
  writeOpcode(COP_Field);
}


void BytecodeWriter::reduceSlot(Slot *E) {
  writeOpcode(COP_Slot);
  Writer->writeUInt16(E->modifiers());
}


void BytecodeWriter::reduceRecord(Record *E) {
  writeOpcode(COP_Record);
  Writer->writeUInt32(E->slots().size());
}


void BytecodeWriter::reduceScalarType(ScalarType *E) {
  writeOpcode(COP_ScalarType);
  writeBaseType(E->baseType());
}


void BytecodeWriter::reduceSCFG(SCFG *E) {
  writeOpcode(COP_SCFG);
  Writer->writeUInt32(E->numBlocks());
}


void BytecodeWriter::reduceBasicBlock(BasicBlock *E) {
  writeOpcode(COP_BasicBlock);
  Writer->writeUInt32(E->blockID());
  Writer->writeUInt32(E->arguments().size());
  Writer->writeUInt32(E->instructions().size());
}


void BytecodeWriter::reduceLiteral(Literal *E) {
  writeOpcode(COP_Literal);
  writeBaseType(E->baseType());
}


void BytecodeWriter::reduceVariable(Variable *E) {
  writeOpcode(COP_Variable);
  Writer->writeUInt32(E->variableDecl()->varIndex());
}


void BytecodeWriter::reduceApply(Apply *E) {
  writeOpcode(COP_Apply);
  writeFlag(E->applyKind());
}


void BytecodeWriter::reduceProject(Project *E) {
  writeOpcode(COP_Project);
  Writer->writeString(E->slotName());
}


void BytecodeWriter::reduceCall(Call *E) {
  writeOpcode(COP_Call);
}


void BytecodeWriter::reduceAlloc(Alloc *E) {
  writeOpcode(COP_Alloc);
  writeFlag(E->allocKind());
}


void BytecodeWriter::reduceLoad(Load *E) {
  writeOpcode(COP_Load);
}


void BytecodeWriter::reduceStore(Store *E) {
  writeOpcode(COP_Store);
}


void BytecodeWriter::reduceArrayIndex(ArrayIndex *E) {
  writeOpcode(COP_ArrayIndex);
}


void BytecodeWriter::reduceArrayAdd(ArrayAdd *E) {
  writeOpcode(COP_ArrayAdd);
}


void BytecodeWriter::reduceUnaryOp(UnaryOp *E) {
  writeOpcode(COP_UnaryOp);
  writeFlag(E->unaryOpcode());
  writeBaseType(E->baseType());
}


void BytecodeWriter::reduceBinaryOp(BinaryOp *E) {
  writeOpcode(COP_BinaryOp);
  writeFlag(E->binaryOpcode());
  writeBaseType(E->baseType());
}


void BytecodeWriter::reduceCast(Cast *E) {
  writeOpcode(COP_Cast);
  writeFlag(E->castOpcode());
  writeBaseType(E->baseType());
}


void BytecodeWriter::reducePhi(Phi *E) {
  writeOpcode(COP_Phi);
}


void BytecodeWriter::reduceGoto(Goto *E) {
  writeOpcode(COP_Goto);
  Writer->writeUInt32(E->targetBlock()->arguments().size());
  Writer->writeUInt32(E->targetBlock()->blockID());
}


void BytecodeWriter::reduceBranch(Branch *E) {
  writeOpcode(COP_Branch);
  Writer->writeUInt32(E->thenBlock()->blockID());
  Writer->writeUInt32(E->elseBlock()->blockID());
}


void BytecodeWriter::reduceReturn(Return *E) {
  writeOpcode(COP_Return);
}


void BytecodeWriter::reduceUndefined(Undefined *E) {
  writeOpcode(COP_Undefined);
}


void BytecodeWriter::reduceWildcard(Wildcard *E) {
  writeOpcode(COP_Wildcard);
}


void BytecodeWriter::reduceIdentifier(Identifier *E) {
  writeOpcode(COP_Identifier);
  Writer->writeString(E->idString());
}


void BytecodeWriter::reduceLet(Let *E) {
  writeOpcode(COP_Let);
}


void BytecodeWriter::reduceIfThenElse(IfThenElse *E) {
  writeOpcode(COP_IfThenElse);
}


}  // end namespace til
}  // end namespace ohmu
