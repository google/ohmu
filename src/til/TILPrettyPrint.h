//===- TILPrettyPrint.h ----------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines pretty printing operations for the ohmu typed intermediate
// language.
//
// UNDER CONSTRUCTION.  USE AT YOUR OWN RISK.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_TIL_TILPRETTYPRINT_H
#define OHMU_TIL_TILPRETTYPRINT_H

#include "TIL.h"

#include <ostream>

namespace ohmu {
namespace til {

/// Helper class to automatically increment and decrement a counter.
class AutoIncDec {
public:
  AutoIncDec(unsigned *P) : IPtr(P) { ++(*P); }
  ~AutoIncDec() { --(*IPtr); }
private:
  unsigned *IPtr;
};


/// Pretty printer for TIL expressions
template <typename Self, typename StreamType>
class PrettyPrinter {
protected:
  Self *self() { return reinterpret_cast<Self *>(this); }

  void indent()   { Indent += 2; }
  void unindent() { Indent -= 2; }

  void newline(StreamType &SS) {
    SS << "\n";
    for (unsigned i = 0; i < Indent; ++i)
      SS << " ";
  }

  // TODO: further distinguish between binary operations.
  static const unsigned Prec_Atom = 0;
  static const unsigned Prec_Postfix = 1;
  static const unsigned Prec_Unary = 2;
  static const unsigned Prec_Binary = 3;
  static const unsigned Prec_Other = 4;
  static const unsigned Prec_Decl = 5;
  static const unsigned Prec_MAX = 6;

  // Return the precedence of a given node, for use in pretty printing.
  unsigned precedence(const SExpr *E) {
    switch (E->opcode()) {
      case COP_VarDecl:    return Prec_Atom;
      case COP_Function:   return Prec_Decl;
      case COP_Code:       return Prec_Decl;
      case COP_Field:      return Prec_Decl;
      case COP_Slot:       return Prec_Decl;
      case COP_Record:     return Prec_Atom;
      case COP_ScalarType: return Prec_Atom;

      case COP_Literal:    return Prec_Atom;
      case COP_Variable:   return Prec_Atom;
      case COP_Apply:      return Prec_Postfix;
      case COP_Project:    return Prec_Postfix;

      case COP_Call:       return Prec_Postfix;
      case COP_Alloc:      return Prec_Other;
      case COP_Load:       return Prec_Postfix;
      case COP_Store:      return Prec_Other;
      case COP_ArrayIndex: return Prec_Postfix;
      case COP_ArrayAdd:   return Prec_Postfix;

      case COP_UnaryOp:    return Prec_Unary;
      case COP_BinaryOp:   return Prec_Binary;
      case COP_Cast:       return Prec_Atom;

      case COP_SCFG:       return Prec_Decl;
      case COP_BasicBlock: return Prec_MAX;
      case COP_Phi:        return Prec_Atom;
      case COP_Goto:       return Prec_Atom;
      case COP_Branch:     return Prec_Atom;
      case COP_Return:     return Prec_Other;

      case COP_Future:     return Prec_Atom;
      case COP_Undefined:  return Prec_Atom;
      case COP_Wildcard:   return Prec_Atom;

      case COP_Identifier: return Prec_Atom;
      case COP_Let:        return Prec_Atom;
      case COP_IfThenElse: return Prec_Decl;
    }
    return Prec_MAX;
  }

  void printBlockLabel(StreamType & SS, const BasicBlock *BB, int index) {
    if (!BB) {
      SS << "BB_null";
      return;
    }
    SS << "BB_";
    SS << BB->blockID();
    if (index >= 0) {
      SS << ":";
      SS << index;
    }
  }

  void printVarName(StreamType &SS, StringRef N, unsigned Id) {
    if (N.size() > 0) {
      SS << N;
      if (Verbose)
        SS << Id;
    }
    else {
      SS << "y_" << Id;
    }
  }

  void printInstrName(StreamType &SS, StringRef N, unsigned Id) {
    if (N.size() > 0) {
      SS << "_";
      SS << N;
      if (Verbose)
        SS << Id;
    }
    else {
      SS << "_x" << Id;
    }
  }

  void printSExpr(const SExpr *E, StreamType &SS, unsigned P, bool Sub=true) {
    AutoIncDec  Aid(&Depth);
    if (Depth > MaxDepth) {
      SS << "...";
      return;
    }

    if (!E) {
      self()->printNull(SS);
      return;
    }
    if (Sub) {
      if (const auto *I = E->asCFGInstruction()) {
        printInstrName(SS, I->instrName(), I->instrID());
        return;
      }
    }
    if (self()->precedence(E) > P) {
      // Wrap expr in () if necessary.
      SS << "(";
      self()->printSExpr(E, SS, Prec_MAX);
      SS << ")";
      return;
    }

    switch (E->opcode()) {
#define TIL_OPCODE_DEF(X)                                                  \
    case COP_##X:                                                          \
      self()->print##X(cast<X>(E), SS);                                    \
      return;
#include "TILOps.def"
    }
  }

  void printNull(StreamType &SS) {
    SS << "#null";
  }

  void printScalarType(const ScalarType *E, StreamType &SS) {
    SS << E->baseType().getTypeName();
  }


  template<class T>
  void printLiteralT(const LiteralT<T> *E, StreamType &SS) {
    SS << E->value();
  }

  void printLiteralT(const LiteralT<uint8_t> *E, StreamType &SS) {
    SS << "'" << E->value() << "'";
  }

  void printLiteralT(const LiteralT<bool> *E, StreamType &SS) {
    if (E->as<bool>()->value())
      SS << "true";
    else
      SS << "false";
  }

  void printLiteralT(const LiteralT<StringRef> *E, StreamType &SS) {
    SS << "\"";
    printLiteralT(E->as<StringRef>(), SS);
    SS << "\"";
  }

  void printLiteralT(const LiteralT<void*> *E, StreamType &SS) {
    if (E->value() == nullptr)
      SS << "null";
    else
      SS << "#ptr";
  }

  template<class Ty>
  class LiteralPrinter {
  public:
    typedef bool ReturnType;

    static bool defaultAction(PrettyPrinter*, const Literal*, StreamType *SS) {
      *SS << "void";
      return false;
    }

    static bool action(PrettyPrinter *Pr, const Literal *E, StreamType *SS) {
      Pr->printLiteralT<Ty>(E->as<Ty>(), *SS);
      return true;
    }
  };

  void printLiteral(const Literal *E, StreamType &SS) {
    BtBr<LiteralPrinter>::branch(E->baseType(), this, E, &SS);
  }

  void printVariable(const Variable *E, StreamType &SS) {
    auto* Vd = E->variableDecl();
    printVarName(SS, Vd->varName(), Vd->varIndex());
  }

  void printVarDecl(const VarDecl *E, StreamType &SS) {
    if (E->kind() == VarDecl::VK_SFun)
      SS << "@";
    printVarName(SS, E->varName(), E->varIndex());
    switch (E->kind()) {
    case VarDecl::VK_Fun:
      SS << ": ";
      break;
    case VarDecl::VK_SFun:
      return;
    case VarDecl::VK_Let:
      SS << " = ";
      break;
    }
    printSExpr(E->definition(), SS, Prec_Decl);
  }

  void printFunction(const Function *E, StreamType &SS, unsigned sugared = 0) {
    switch (sugared) {
      default:
        SS << "\\(";   // Lambda
        break;
      case 1:
        SS << "(";     // Slot declarations
        break;
      case 2:
        SS << ", ";    // Curried functions
        break;
    }
    self()->printVarDecl(E->variableDecl(), SS);

    const SExpr *B = E->body();
    if (B && B->opcode() == COP_Function) {
      self()->printFunction(cast<Function>(B), SS, 2);
    }
    else {
      SS << ") ";
      self()->printSExpr(B, SS, Prec_Decl);
    }
  }

  void printCode(const Code *E, StreamType &SS) {
    SS << ": ";
    self()->printSExpr(E->returnType(), SS, Prec_Decl-1);
    SS << " -> ";
    if (E->body())
      self()->printSExpr(E->body(), SS, Prec_Decl);
    else
      SS << "_";
  }

  void printField(const Field *E, StreamType &SS) {
    SS << ": ";
    self()->printSExpr(E->range(), SS, Prec_Decl-1);
    SS << " = ";
    if (E->body())
      self()->printSExpr(E->body(), SS, Prec_Decl);
    else
      SS << "_";
  }

  void printSlot(const Slot *E, StreamType &SS) {
    SS << E->slotName();
    if (auto *Fn = dyn_cast<Function>(E->definition())) {
      printFunction(Fn, SS, 1);
    }
    else if (auto *Cd = dyn_cast<Code>(E->definition())) {
      SS << "()";
      printCode(Cd, SS);
    }
    else if (auto *Fld = dyn_cast<Field>(E->definition())) {
      printField(Fld, SS);
    }
    else {
      SS << " = ";
      self()->printSExpr(E->definition(), SS, Prec_Decl);
    }
    SS << ";";
  }

  void printRecord(const Record *E, StreamType &SS) {
    SS << "struct ";
    if (E->parent()) {
      self()->printSExpr(E->parent(), SS, Prec_Decl);
      SS << " ";
    }
    SS << "{";
    self()->indent();
    for (auto &S : E->slots()) {
      self()->newline(SS);
      self()->printSlot(S.get(), SS);
    }
    self()->unindent();
    self()->newline(SS);
    SS << "}";
  }

  void printApply(const Apply *E, StreamType &SS, bool sugared = false) {
    const SExpr *F = E->fun();

    if (E->isSelfApplication()) {
      self()->printSExpr(F, SS, Prec_Postfix);
      if (E->isDelegation()) {
        SS << "@(";
        self()->printSExpr(E->arg(), SS, Prec_MAX);
        SS << ")";
      }
      else if (Verbose)
        SS << "@()";
      return;
    }

    const Apply *FA = dyn_cast<Apply>(F);
    if (FA && !FA->isSelfApplication()) {
      printApply(cast<Apply>(F), SS, true);
      SS << ", ";
    } else {
      self()->printSExpr(F, SS, Prec_Postfix);
      SS << "(";
    }
    self()->printSExpr(E->arg(), SS, Prec_MAX);
    if (!sugared)
      SS << ")";
  }

  void printProject(const Project *E, StreamType &SS) {
    if (!E->record()) {
      if (Verbose)
        SS << "_global.";
      SS << E->slotName();
      return;
    }
    if (CStyle) {
      // Omit the 'this->'
      if (const Apply *SAP = dyn_cast<Apply>(E->record())) {
        if (auto *V = dyn_cast<Variable>(SAP->fun())) {
          if (V->variableDecl()->kind() == VarDecl::VK_SFun &&
              !SAP->isDelegation()) {
            SS << E->slotName();
            return;
          }
        }
      }
      if (isa<Wildcard>(E->record())) {
        // handle existentials
        SS << "&";
        SS << E->slotName();  // E->clangDecl()->getQualifiedNameAsString();
        return;
      }
    }
    self()->printSExpr(E->record(), SS, Prec_Postfix);
    if (CStyle && E->isArrow()) {
      SS << "->";
    }
    else {
      SS << ".";
    }
    SS << E->slotName();
  }

  void printCall(const Call *E, StreamType &SS) {
    const SExpr *T = E->target();
    if (T && T->opcode() == COP_Apply) {
      self()->printApply(cast<Apply>(T), SS, true);
      SS << ")";
      if (Verbose)
        SS << "()";
    }
    else {
      self()->printSExpr(T, SS, Prec_Postfix);
      SS << "()";
    }
  }

  void printAlloc(const Alloc *E, StreamType &SS) {
    SS << "new ";
    self()->printSExpr(E->initializer(), SS, Prec_Other-1);
  }

  void printLoad(const Load *E, StreamType &SS) {
    self()->printSExpr(E->pointer(), SS, Prec_Postfix);
    if (!CStyle)
      SS << "^";
  }

  void printStore(const Store *E, StreamType &SS) {
    self()->printSExpr(E->destination(), SS, Prec_Other-1);
    SS << " := ";
    self()->printSExpr(E->source(), SS, Prec_Other-1);
  }

  void printArrayIndex(const ArrayIndex *E, StreamType &SS) {
    self()->printSExpr(E->array(), SS, Prec_Postfix);
    SS << "[";
    self()->printSExpr(E->index(), SS, Prec_MAX);
    SS << "]";
  }

  void printArrayAdd(const ArrayAdd *E, StreamType &SS) {
    self()->printSExpr(E->array(), SS, Prec_Postfix);
    SS << " [+] ";
    self()->printSExpr(E->index(), SS, Prec_Atom);
  }

  void printUnaryOp(const UnaryOp *E, StreamType &SS) {
    SS << getUnaryOpcodeString(E->unaryOpcode());
    self()->printSExpr(E->expr(), SS, Prec_Unary);
  }

  void printBinaryOp(const BinaryOp *E, StreamType &SS) {
    self()->printSExpr(E->expr0(), SS, Prec_Binary-1);
    SS << " " << getBinaryOpcodeString(E->binaryOpcode()) << " ";
    self()->printSExpr(E->expr1(), SS, Prec_Binary-1);
  }

  void printCast(const Cast *E, StreamType &SS) {
    if (!CStyle) {
      SS << "cast.";
      SS << getCastOpcodeString(E->castOpcode());
      SS << "(";
      self()->printSExpr(E->expr(), SS, Prec_Unary);
      SS << ")";
      return;
    }
    self()->printSExpr(E->expr(), SS, Prec_Unary);
  }

  void printSCFG(const SCFG *E, StreamType &SS) {
    SS << "CFG {";
    self()->indent();
    bool First = true;
    for (auto &B : E->blocks()) {
      self()->newline(SS);
      if (!First)
        self()->newline(SS);
      First = false;
      printBasicBlock(B.get(), SS);
    }
    self()->unindent();
    self()->newline(SS);
    SS << "}";
  }


  void printBBInstr(const Instruction *E, StreamType &SS) {
    if (!E) {
      if (Verbose) {
        self()->newline(SS);
        SS << "null;";
      }
      return;
    }
    self()->newline(SS);
    if (E->opcode() != COP_Store) {
      SS << "let ";
      printInstrName(SS, E->instrName(), E->instrID());
      if (Verbose) {
        SS << ": " << E->baseType().getTypeName();
      }
      SS << " = ";
    }
    self()->printSExpr(E, SS, Prec_MAX, false);
    SS << ";";
  }

  void printBasicBlock(const BasicBlock *E, StreamType &SS) {
    printBlockLabel(SS, E, -1);
    SS << ": // ";

    SS << "preds={";
    bool First = true;
    for (auto &B : E->predecessors()) {
      if (!First)
        SS << ", ";
      printBlockLabel(SS, B.get(), -1);
      First = false;
    }
    SS << "}";

    SS << " dom=";
    printBlockLabel(SS, E->parent(), -1);
    SS << " post=";
    printBlockLabel(SS, E->postDominator(), -1);

    self()->indent();

    for (auto *A : E->arguments()) {
      printBBInstr(A, SS);
    }
    for (auto *I : E->instructions()) {
      printBBInstr(I, SS);
    }
    auto *T = E->terminator();
    if (T) {
      self()->newline(SS);
      self()->printSExpr(T, SS, Prec_MAX, false);
      SS << ";";
    }
    self()->unindent();
  }

  void printPhi(const Phi *E, StreamType &SS) {
    SS << "phi(";
    if (E->status() == Phi::PH_SingleVal)
      self()->printSExpr(E->values()[0].get(), SS, Prec_MAX);
    else {
      unsigned i = 0;
      for (auto &V : E->values()) {
        if (i++ > 0)
          SS << ", ";
        self()->printSExpr(V.get(), SS, Prec_MAX);
      }
    }
    SS << ")";
  }

  void printGoto(const Goto *E, StreamType &SS) {
    SS << "goto ";
    printBlockLabel(SS, E->targetBlock(), E->phiIndex());
  }

  void printBranch(const Branch *E, StreamType &SS) {
    SS << "branch (";
    self()->printSExpr(E->condition(), SS, Prec_MAX);
    SS << ") ";
    printBlockLabel(SS, E->thenBlock(), -1);
    SS << " ";
    printBlockLabel(SS, E->elseBlock(), -1);
  }

  void printReturn(const Return *E, StreamType &SS) {
    SS << "return ";
    self()->printSExpr(E->returnValue(), SS, Prec_Other);
  }

  void printIdentifier(const Identifier *E, StreamType &SS) {
    SS << "$";
    SS << E->idString();
  }

  void printLet(const Let *E, StreamType &SS, bool Nested=false) {
    if (!Nested) {
      SS << "{";
      self()->indent();
    }

    self()->newline(SS);
    SS << "let ";
    printVarDecl(E->variableDecl(), SS);
    SS << ";";

    if (auto *L = dyn_cast<Let>(E->body())) {
      printLet(L, SS, true);
    }
    else {
      self()->newline(SS);
      printSExpr(E->body(), SS, Prec_Decl);
      SS << ";";
    }

    if (!Nested) {
      self()->unindent();
      self()->newline(SS);
      SS << "}";
    }
  }

  void printIfThenElse(const IfThenElse *E, StreamType &SS) {
    if (CStyle) {
      printSExpr(E->condition(), SS, Prec_Unary);
      SS << " ? ";
      printSExpr(E->thenExpr(), SS, Prec_Unary);
      SS << " : ";
      printSExpr(E->elseExpr(), SS, Prec_Unary);
      return;
    }
    SS << "if (";
    printSExpr(E->condition(), SS, Prec_MAX);
    SS << ") then ";
    printSExpr(E->thenExpr(), SS, Prec_Other);
    SS << " else ";
    printSExpr(E->elseExpr(), SS, Prec_Other);
  }

  void printFuture(const Future *E, StreamType &SS) {
    if (E->maybeGetResult()) {
      SS << "#f(";
      self()->printSExpr(E->maybeGetResult(), SS, Prec_MAX);
      SS << ")";
    }
    else
      SS << "#future";
  }

  void printUndefined(const Undefined *E, StreamType &SS) {
    SS << "#undefined";
  }

  void printWildcard(const Wildcard *E, StreamType &SS) {
    SS << "*";
  }


public:
  PrettyPrinter(bool V = false, bool CS = true)
     : Verbose(V), CStyle(CS), Indent(0), Depth(0)
  {}

  static void print(const SExpr *E, StreamType &SS, bool Sub=false) {
    Self printer;
    printer.printSExpr(E, SS, Prec_MAX, Sub);
  }

private:
  const unsigned MaxDepth = 128;

  bool Verbose;  // Print out additional information
  bool CStyle;   // Print exprs in C-like syntax.
  unsigned Indent;
  unsigned Depth;
};


class StdPrinter : public PrettyPrinter<StdPrinter, std::ostream> { };

class TILDebugPrinter : public PrettyPrinter<TILDebugPrinter, std::ostream> {
public:
  TILDebugPrinter() : PrettyPrinter(true, false) { }
};


#ifdef OHMU_STANDALONE
inline DiagnosticStream& operator<<(DiagnosticStream& Ds, SExpr *E) {
  TILDebugPrinter::print(E, Ds.outputStream(), false);
  return Ds;
}
#endif


}  // end namespace til
}  // end namespace ohmu


#endif  // OHMU_TIL_TILPRETTYPRINT_H

