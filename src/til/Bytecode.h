//===- Bytecode.h ----------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//


#include "TIL.h"
#include "TILTraverse.h"

#include <iostream>

namespace ohmu {
namespace til {


class BytecodeWriter : public Traversal<BytecodeWriter>,
                       public DefaultScopeHandler {
public:
  /** Reducer Methods **/

  void reduceWeak(Instruction *I) {
    writeCodeStr("wi");
    writeInt(I->instrID());
  }

  void reduceNull() {
    writeCodeStr("__");
  }

  void reduceVarDecl(VarDecl* E) {
    writeOpcode(COP_VarDecl);
    writeCode8(E->kind());
    writeInt(E->varIndex());
  }

  void reduceFunction(Function *E) {
    writeOpcode(COP_Function);
  }

  void reduceCode(Code *E) {
    writeOpcode(COP_Code);
    writeCode8(E->callingConvention());
  }

  void reduceField(Field *E) {
    writeOpcode(COP_Field);
  }

  void reduceSlot(Slot *E) {
    writeOpcode(COP_Slot);
    writeCode16(E->modifiers());
  }

  void reduceRecord(Record *E) {
    writeOpcode(COP_Record);
    writeInt(E->slots().size());
  }

  void reduceScalarType(ScalarType *E) {
    writeOpcode(COP_ScalarType);
    writeBaseType(E->baseType());
  }

  void reduceSCFG(SCFG *E) {
    writeOpcode(COP_SCFG);
    writeInt(E->numBlocks());
  }

  void reduceBasicBlock(BasicBlock *E) {
    writeOpcode(COP_BasicBlock);
    writeInt(E->arguments().size());
    writeInt(E->instructions().size());
  }

  template<class Ty>
  void reduceLiteralT(LiteralT<Ty>* E) {
    Ty Val = E->value();
    writeBytes(sizeof(Ty), &Val);
  }

  void reduceLiteral(Literal *E) {
    writeOpcode(COP_Literal);
    writeBaseType(E->baseType());
  }

  void reduceVariable(Variable *E) {
    writeOpcode(COP_Variable);
    writeInt(E->variableDecl()->varIndex());
  }

  void reduceApply(Apply *E) {
    writeOpcode(COP_Apply);
    writeCode8(E->applyKind());
  }

  void reduceProject(Project *E) {
    writeOpcode(COP_Project);
    writeString(E->slotName());
  }

  void reduceCall(Call *E) {
    writeOpcode(COP_Call);
  }

  void reduceAlloc(Alloc *E) {
    writeOpcode(COP_Alloc);
    writeCode8(E->allocKind());
  }

  void reduceLoad(Load *E) {
    writeOpcode(COP_Load);
  }

  void reduceStore(Store *E) {
    writeOpcode(COP_Store);
  }

  void reduceArrayIndex(ArrayIndex *E) {
    writeOpcode(COP_ArrayIndex);
  }

  void reduceArrayAdd(ArrayAdd *E) {
    writeOpcode(COP_ArrayAdd);
  }

  void reduceUnaryOp(UnaryOp *E) {
    writeOpcode(COP_UnaryOp);
    writeUnaryOpcode(E->unaryOpcode());
    writeBaseType(E->baseType());
  }

  void reduceBinaryOp(BinaryOp *E) {
    writeOpcode(COP_BinaryOp);
    writeBinaryOpcode(E->binaryOpcode());
    writeBaseType(E->baseType());
  }

  void reduceCast(Cast *E) {
    writeOpcode(COP_Cast);
    writeCastOpcode(E->castOpcode());
    writeBaseType(E->baseType());
  }

  void reducePhi(Phi *E) {
    writeOpcode(COP_Phi);
  }

  void reduceGoto(Goto *E) {
    writeOpcode(COP_Goto);
    writeInt(E->targetBlock()->arguments().size());
    writeInt(E->targetBlock()->blockID());
  }

  void reduceBranch(Branch *E) {
    writeOpcode(COP_Branch);
    writeInt(E->thenBlock()->blockID());
    writeInt(E->elseBlock()->blockID());
  }

  void reduceReturn(Return *E) {
    writeOpcode(COP_Return);
  }

  void reduceUndefined(Undefined *E) {
    writeOpcode(COP_Undefined);
  }

  void reduceWildcard(Wildcard *E) {
    writeOpcode(COP_Wildcard);
  }

  void reduceIdentifier(Identifier *E) {
    writeOpcode(COP_Identifier);
    writeString(E->idString());
  }

  void reduceLet(Let *E) {
    writeOpcode(COP_Let);
  }

  void reduceIfThenElse(IfThenElse *E) {
    writeOpcode(COP_IfThenElse);
  }

  /** Other public methods **/

  static void write(std::ostream& Out, SExpr* E) {
    BytecodeWriter Writer(Out);
    Writer.traverseAll(E);
  }

  BytecodeWriter(std::ostream& Out) : Outstr(Out) { }

private:
  void writeChar(char C) {
    Outstr << C;
  }

  void writeCodeStr(const char* Str) {
    Outstr << Str;
  }

  void writeCode8 (uint8_t V) {
    Outstr << static_cast<unsigned int>(V);
    //Outstr << static_cast<char>(V);
  }

  void writeCode16(uint16_t V) {
    Outstr << static_cast<unsigned int>(V);
    //writeBytes(2, &V);
  }

  void writeInt(int32_t V) {
    Outstr << V;
    //writeBytes(4, &i);
  }

  void writeString(StringRef S) {
    writeInt(S.size());
    Outstr << S;
  }

  void writeBytes(unsigned Nb, const void* Bytes) {
    const char* Buf = reinterpret_cast<const char*>(Bytes);
    for (unsigned i = 0; i < Nb; ++i)
      Outstr << Buf[i];
  }

  void writeOpcode(TIL_Opcode Op);
  void writeUnaryOpcode(TIL_UnaryOpcode Op);
  void writeBinaryOpcode(TIL_BinaryOpcode Op);
  void writeCastOpcode(TIL_CastOpcode Op);
  void writeBaseType(BaseType Bt);

private:
  std::ostream& Outstr;
};


}  // end namespace til
}  // end namespace ohmu

