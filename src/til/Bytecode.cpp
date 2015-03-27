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


void BytecodeWriter::writeOpcode(TIL_Opcode Op) {
  switch(Op) {
  case COP_VarDecl:    writeCodeStr("Vd");  break;
  case COP_Function:   writeCodeStr("Fn");  break;
  case COP_Code:       writeCodeStr("Cd");  break;
  case COP_Field:      writeCodeStr("Fl");  break;
  case COP_Slot:       writeCodeStr("Sl");  break;
  case COP_Record:     writeCodeStr("Rd");  break;
  case COP_ScalarType: writeCodeStr("Ty");  break;
  case COP_SCFG:       writeCodeStr("Cg");  break;
  case COP_BasicBlock: writeCodeStr("Bb");  break;
  case COP_Literal:    writeCodeStr("Ll");  break;
  case COP_Variable:   writeCodeStr("Vr");  break;
  case COP_Apply:      writeCodeStr("Ap");  break;
  case COP_Project:    writeCodeStr("Pj");  break;
  case COP_Call:       writeCodeStr("Cl");  break;
  case COP_Alloc:      writeCodeStr("Al");  break;
  case COP_Load:       writeCodeStr("Ld");  break;
  case COP_Store:      writeCodeStr("Sr");  break;
  case COP_ArrayIndex: writeCodeStr("Ai");  break;
  case COP_ArrayAdd:   writeCodeStr("Aa");  break;
  case COP_UnaryOp:    writeCodeStr("Uo");  break;
  case COP_BinaryOp:   writeCodeStr("Bo");  break;
  case COP_Cast:       writeCodeStr("Co");  break;
  case COP_Phi:        writeCodeStr("Ph");  break;
  case COP_Goto:       writeCodeStr("Gt");  break;
  case COP_Branch:     writeCodeStr("Br");  break;
  case COP_Return:     writeCodeStr("Rt");  break;
  case COP_Future:     break;
  case COP_Undefined:  writeCodeStr("Ud");  break;
  case COP_Wildcard:   writeCodeStr("**");  break;
  case COP_Identifier: writeCodeStr("Id");  break;
  case COP_Let:        writeCodeStr("Lt");  break;
  case COP_IfThenElse: writeCodeStr("If");  break;
  }
}


void BytecodeWriter::writeBinaryOpcode(TIL_BinaryOpcode Bop) {
  writeCode8('a' + Bop);
}


void BytecodeWriter::writeUnaryOpcode(TIL_UnaryOpcode Uop) {
  writeCode8('a' + Uop);
}


void BytecodeWriter::writeCastOpcode(TIL_CastOpcode Cop) {
  writeCode8('a' + Cop);
}



void BytecodeWriter::writeBaseType(BaseType Bt) {
  switch (Bt.Base) {
    case BaseType::BT_Void:        writeChar('v'); break;
    case BaseType::BT_Bool:        writeChar('b'); break;
    case BaseType::BT_Int:         writeChar('i'); break;
    case BaseType::BT_UnsignedInt: writeChar('u'); break;
    case BaseType::BT_Float:       writeChar('f'); break;
    case BaseType::BT_String:      writeChar('s'); break;
    case BaseType::BT_Pointer:     writeChar('p'); break;
  }
  switch (Bt.Size) {
    case BaseType::ST_0:   writeChar('0'); break;
    case BaseType::ST_1:   writeChar('1'); break;
    case BaseType::ST_8:   writeChar('2'); break;
    case BaseType::ST_16:  writeChar('3'); break;
    case BaseType::ST_32:  writeChar('4'); break;
    case BaseType::ST_64:  writeChar('5'); break;
    case BaseType::ST_128: writeChar('6'); break;
  }
}


}  // end namespace til
}  // end namespace ohmu
