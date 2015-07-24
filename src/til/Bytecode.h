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

#include "AnnotationImpl.h"
#include "CFGBuilder.h"
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

  enum PseudoOpcode : uint8_t {
    PSOP_Null = 0,
    PSOP_WeakInstrRef,
    PSOP_BBArgument,
    PSOP_BBInstruction,
    PSOP_EnterScope,
    PSOP_ExitScope,
    PSOP_EnterBlock,
    PSOP_EnterCFG,
    PSOP_EnterAnn,
    PSOP_Last
  };

  enum PseudoAnnKind : uint8_t {
    PSANN_ExitAnn = 0,
    PSANN_Last
  };

  void getBitSize(uint32_t) { }  // trigger an error for unhandled types.

  unsigned getBitSizeImpl(PseudoOpcode)     { return 6; }
  unsigned getBitSizeImpl(TIL_Opcode)       { return 6; }
  unsigned getBitSizeImpl(PseudoAnnKind)    { return 6; }
  unsigned getBitSizeImpl(TIL_AnnKind)      { return 6; }
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
  ByteStreamWriterBase() : Pos(0), Buffer(BufferSize) { }

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
  int length() { return BufferSize - Pos; }

  /// Size of the buffer.  Default is 64k.
  static const int BufferSize = BytecodeBase::MaxRecordSize << 4;

  int Pos;
  std::vector<uint8_t> Buffer;
};



/// Abstract base class for an input stream of bytes.
/// Derived classes must implement readData to read the binary data from
/// a source.  (E.g. file, network, etc.)
class ByteStreamReaderBase {
public:
  ByteStreamReaderBase() : BufferLen(0), Pos(0), Eof(false), Error(false),
  Buffer(BufferSize) { }

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

  bool empty() { return Eof && length() <= 0; }

private:
  /// Return the remaining data in the buffer.
  int length() { return BufferLen - Pos; }

  /// Size of the buffer.  Default is 64k.
  static const int BufferSize = BytecodeBase::MaxRecordSize << 4;

  int     BufferLen;
  int     Pos;
  bool    Eof;
  bool    Error;
  std::vector<uint8_t> Buffer;
};



/// Traverse a SExpr and serialize it.
class BytecodeWriter : public Traversal<BytecodeWriter>,
                       public BytecodeBase {
protected:
  typedef Traversal<BytecodeWriter> SuperTv;

  template<class T>
  void writeFlag(T Flag) { Writer->writeBits32(Flag, getBitSize<T>()); }

  void writePseudoOpcode(PseudoOpcode Psop) {
    // std::cerr << "Psop: " << Psop << "\n";
    writeFlag(Psop);
  }

  void writeOpcode(TIL_Opcode Op) {
    // std::cerr << "Op: " << getOpcodeString(Op) << "\n";
    unsigned Psop = static_cast<unsigned>(PSOP_Last) + Op;
    writePseudoOpcode(static_cast<PseudoOpcode>(Psop));
  }

  void writePseudoAnnKind(PseudoAnnKind Psann) {
    writeFlag(Psann);
  }

  void writeBaseType(BaseType Bt) {
    Writer->writeUInt8(Bt.asUInt8());
    if (Bt.VectSize >= 1)
      Writer->writeUInt8(Bt.VectSize);
  }

  void writeLitVal(bool V)      { Writer->writeBool(V); }

  void writeLitVal(uint8_t  V)  { Writer->writeUInt8(V);  }
  void writeLitVal(uint16_t V)  { Writer->writeUInt16(V); }
  void writeLitVal(uint32_t V)  { Writer->writeUInt32(V); }
  void writeLitVal(uint64_t V)  { Writer->writeUInt64(V); }

  void writeLitVal(int8_t  V)   { Writer->writeInt8(V);  }
  void writeLitVal(int16_t V)   { Writer->writeInt16(V); }
  void writeLitVal(int32_t V)   { Writer->writeInt32(V); }
  void writeLitVal(int64_t V)   { Writer->writeInt64(V); }

  void writeLitVal(float  V)    { Writer->writeFloat(V);  }
  void writeLitVal(double V)    { Writer->writeDouble(V); }

  void writeLitVal(StringRef S) { Writer->writeString(S); }

  void writeLitVal(void* P) {
    assert(P == nullptr && "Cannot serialize non-null pointer literal.");
  }

public:
  template<class T>
  void traverse(T *E, TraversalKind K) {
    if (WritingAnn)
      writePseudoAnnKind(PSANN_ExitAnn);
    bool PrevWriting = WritingAnn;

    WritingAnn = false;
    SuperTv::traverse(E, K);
    Writer->endRecord();

    if (E->annotations()) {
      writePseudoOpcode(PSOP_EnterAnn);
      WritingAnn = true;
      postTraverseAllAnnotations(E->annotations());
      WritingAnn = PrevWriting;
      if (!WritingAnn)
        writePseudoAnnKind(PSANN_ExitAnn);
    } else {
      WritingAnn = PrevWriting;
      if (WritingAnn)
        writePseudoOpcode(PSOP_EnterAnn);
    }
  }

  // Postpone traversal until their SExpr is fully written.
  void traverseAllAnnotations(Annotation *A) { }

  void postTraverseAllAnnotations(Annotation *A) {
    while (A != nullptr) {
      self()->traverseAnnotation(A);
      A = A->next();
    }
  }

  template <class T>
  void reduceAnnotationT(T *A) {
    writeAnnKind(A->kind());
    A->serialize(this);
  }

  template<class Ty>
  void reduceLiteralT(LiteralT<Ty>* E) {
    writeOpcode(COP_Literal);
    writeBaseType(E->baseType());
    writeLitVal(E->value());
  }

  void writeAnnKind(TIL_AnnKind Akind) {
    unsigned Psann = static_cast<unsigned>(PSANN_Last) + Akind;
    writePseudoAnnKind(static_cast<PseudoAnnKind>(Psann));
  }

  typedef bool LocationState;
  bool enterSubExpr(TraversalKind K) { return false; }
  void exitSubExpr(TraversalKind K, LocationState S) { }

  void enterScope(VarDecl *Vd);
  void enterCFG  (SCFG *Cfg);
  void enterBlock(BasicBlock *B);

  void exitScope (VarDecl *Vd);
  void exitCFG   (SCFG *Cfg)     { }
  void exitBlock (BasicBlock *B) { }

  void reduceNull();
  void reduceWeak(Instruction *E);
  void reduceBBArgument(Phi *E);
  void reduceBBInstruction(Instruction *E);

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

  BytecodeWriter(ByteStreamWriterBase *W) : Writer(W), WritingAnn(false) { }

  ByteStreamWriterBase *getWriter() { return Writer; }

private:
  ByteStreamWriterBase *Writer;
  bool WritingAnn;
};



/// Deserialize a SExpr.
class BytecodeReader : public BytecodeBase {
protected:
  template<class T>
  T readFlag() { return static_cast<T>(Reader->readBits32(getBitSize<T>())); }

  PseudoOpcode readPseudoOpcode() { return readFlag<PseudoOpcode>(); }

  PseudoAnnKind readPseudoAnnKind() { return readFlag<PseudoAnnKind>(); }

  TIL_Opcode getOpcode(PseudoOpcode Psop) {
    return static_cast<TIL_Opcode>(Psop - PSOP_Last);
  }

  TIL_AnnKind getAnnotationKind(PseudoAnnKind Psann) {
    return static_cast<TIL_AnnKind>(Psann - PSANN_Last);
  }

  BaseType readBaseType() {
    BaseType Bt;
    if (Bt.fromUInt8(Reader->readUInt8()))
      Bt.VectSize = Reader->readUInt8();
    return Bt;
  }

  bool      readLitVal(bool*)      { return Reader->readBool(); }

  uint8_t   readLitVal(uint8_t*)   { return Reader->readUInt8();  }
  uint16_t  readLitVal(uint16_t*)  { return Reader->readUInt16(); }
  uint32_t  readLitVal(uint32_t*)  { return Reader->readUInt32(); }
  uint64_t  readLitVal(uint64_t*)  { return Reader->readUInt64(); }

  int8_t    readLitVal(int8_t*)    { return Reader->readInt8();  }
  int16_t   readLitVal(int16_t*)   { return Reader->readInt16(); }
  int32_t   readLitVal(int32_t*)   { return Reader->readInt32(); }
  int64_t   readLitVal(int64_t*)   { return Reader->readInt64(); }

  float     readLitVal(float*)     { return Reader->readFloat();  }
  double    readLitVal(double*)    { return Reader->readDouble(); }
  StringRef readLitVal(StringRef*) { return Reader->readString(); }
  void*     readLitVal(void**)     { return nullptr; }

  ArrayRef<SExpr*> lastArgs(unsigned n) {
    return ArrayRef<SExpr*>(&Stack[Stack.size() - n], n);
  }

  void fail(const char* Msg) {
    std::cerr << Msg << "\n";
    Success = false;
  }

  template<class T> class ReadLiteralFun;

  /// Read an SExpr from the byte stream.
  void readSExpr();

  /// Read a SExpr, branching on the Opcode.
  void readSExprByType(TIL_Opcode Op);

  /// Read an Ann, branching on the AnnKind.
  void readAnnotationByKind(TIL_AnnKind Ak);

  void enterScope();
  void exitScope();
  void enterBlock();
  void enterCFG();
  void readAllAnnotations();

  /// Get the VarDecl for the given variable index.
  VarDecl* getVarDecl(unsigned Vidx);

  /// Get the Block for the given BlockID.
  /// Nargs is the expected number of arguments.
  BasicBlock* getBlock(unsigned Bid, unsigned Nargs);

  void readNull();
  void readWeak();
  void readBBArgument();
  void readBBInstruction();

  void readVarDecl();
  void readFunction();
  void readCode() ;
  void readField();
  void readSlot();
  void readRecord();
  void readScalarType();
  void readSCFG();
  void readBasicBlock();
  void readLiteral();
  void readVariable();
  void readApply();
  void readProject();
  void readCall();
  void readAlloc();
  void readLoad();
  void readStore();
  void readArrayIndex();
  void readArrayAdd();
  void readUnaryOp();
  void readBinaryOp();
  void readCast();
  void readPhi();
  void readGoto();
  void readBranch();
  void readReturn();
  void readFuture() { }        ///< Can't happen.
  void readUndefined();
  void readWildcard();
  void readIdentifier();
  void readLet();
  void readIfThenElse();

public:
  SExpr* read();

  bool success() { return Success; }

  BytecodeReader(CFGBuilder& B, ByteStreamReaderBase* R)
      : Builder(B), Reader(R), CurrentInstrID(0), Success(true) {
    Vars.push_back(nullptr);  // indices start at 1.
  }

  SExpr  *arg(int i)    { return Stack[Stack.size()-1 - i]; }
  void   push(SExpr* E) { Stack.push_back(E); }
  void   drop(int n)    { Stack.resize(Stack.size() - n); }

  ByteStreamReaderBase *getReader() { return Reader; }

  CFGBuilder& getBuilder() { return Builder; }

private:
  CFGBuilder&            Builder;
  ByteStreamReaderBase*  Reader;

  unsigned                  CurrentInstrID;
  bool                      Success;
  std::vector<SExpr*>       Stack;
  std::vector<VarDecl*>     Vars;
  std::vector<BasicBlock*>  Blocks;
  std::vector<Instruction*> Instrs;
};


}  // end namespace til
}  // end namespace ohmu

#endif  // OHMU_TIL_BYTECODE_H
