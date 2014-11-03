//===- ThreadSafetyTIL.h ---------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT in the llvm repository for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a simple Typed Intermediate Language, or TIL, that is used
// by the thread safety analysis (See ThreadSafety.cpp).  The TIL is intended
// to be largely independent of clang, in the hope that the analysis can be
// reused for other non-C++ languages.  All dependencies on clang/llvm should
// go in ThreadSafetyUtil.h.
//
// Thread safety analysis works by comparing mutex expressions, e.g.
//
// class A { Mutex mu; int dat GUARDED_BY(this->mu); }
// class B { A a; }
//
// void foo(B* b) {
//   (*b).a.mu.lock();     // locks (*b).a.mu
//   b->a.dat = 0;         // substitute &b->a for 'this';
//                         // requires lock on (&b->a)->mu
//   (b->a.mu).unlock();   // unlocks (b->a.mu)
// }
//
// As illustrated by the above example, clang Exprs are not well-suited to
// represent mutex expressions directly, since there is no easy way to compare
// Exprs for equivalence.  The thread safety analysis thus lowers clang Exprs
// into a simple intermediate language (IL).  The IL supports:
//
// (1) comparisons for semantic equality of expressions
// (2) SSA renaming of variables
// (3) wildcards and pattern matching over expressions
// (4) hash-based expression lookup
//
// The TIL is currently very experimental, is intended only for use within
// the thread safety analysis, and is subject to change without notice.
// After the API stabilizes and matures, it may be appropriate to make this
// more generally available to other analyses.
//
// UNDER CONSTRUCTION.  USE AT YOUR OWN RISK.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTIL_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYTIL_H

// All clang include dependencies for this file must be put in
// ThreadSafetyUtil.h.
#include "ThreadSafetyUtil.h"
#include "ThreadSafetyType.h"


#include <stdint.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>


namespace clang {
namespace threadSafety {
namespace til {


/// Enum for the different distinct classes of SExpr
enum TIL_Opcode {
#define TIL_OPCODE_DEF(X) COP_##X,
#include "ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
};

/// Opcode for unary arithmetic operations.
enum TIL_UnaryOpcode : unsigned char {
  UOP_Minus,        ///<  -
  UOP_BitNot,       ///<  ~
  UOP_LogicNot      ///<  !
};

/// Opcode for binary arithmetic operations.
enum TIL_BinaryOpcode : unsigned char {
  BOP_Add,          ///<  +
  BOP_Sub,          ///<  -
  BOP_Mul,          ///<  *
  BOP_Div,          ///<  /
  BOP_Rem,          ///<  %
  BOP_Shl,          ///<  <<
  BOP_Shr,          ///<  >>
  BOP_BitAnd,       ///<  &
  BOP_BitXor,       ///<  ^
  BOP_BitOr,        ///<  |
  BOP_Eq,           ///<  ==
  BOP_Neq,          ///<  !=
  BOP_Lt,           ///<  <
  BOP_Leq,          ///<  <=
  BOP_LogicAnd,     ///<  &&  (no short-circuit)
  BOP_LogicOr       ///<  ||  (no short-circuit)
};

/// Opcode for cast operations.  (Currently Very Incomplete)
enum TIL_CastOpcode : unsigned char {
  CAST_none = 0,
  // numeric casts
  CAST_extendNum,       ///< extend precision of numeric type int->int or fp->fp
  CAST_truncNum,        ///< truncate precision of numeric type
  CAST_intToFloat,      ///< convert integer to floating point type
  CAST_truncToInt,      ///< truncate float f to integer i;  abs(i) <= abs(f)
  CAST_roundToInt,      ///< convert float to nearest integer
  // bit casts
  CAST_toBits,          ///< bitwise cast of pointer or float to integer
  CAST_bitsToFloat,     ///< bitwise cast of integer to float
  CAST_unsafeBitsToPtr, ///< cast integer to pointer  (very unsafe!)
  // pointer casts
  CAST_downCast,        ///< cast pointer type to pointer subtype   (checked)
  CAST_unsafeDownCast,  ///< cast pointer type to pointer subtype   (unchecked)
  CAST_unsafePtrCast,   ///< cast pointer to any other pointer type (unchecked)
  CAST_objToPtr         ///< convert smart pointer to pointer  (C++ only)
};

const TIL_Opcode       COP_Min  = COP_Future;
const TIL_Opcode       COP_Max  = COP_Branch;
const TIL_UnaryOpcode  UOP_Min  = UOP_Minus;
const TIL_UnaryOpcode  UOP_Max  = UOP_LogicNot;
const TIL_BinaryOpcode BOP_Min  = BOP_Add;
const TIL_BinaryOpcode BOP_Max  = BOP_LogicOr;
const TIL_CastOpcode   CAST_Min = CAST_none;
const TIL_CastOpcode   CAST_Max = CAST_objToPtr;

/// Return the name of an opcode.
StringRef getOpcodeString(TIL_Opcode Op);

/// Return the name of a unary opcode.
StringRef getUnaryOpcodeString(TIL_UnaryOpcode Op);

/// Return the name of a binary opcode.
StringRef getBinaryOpcodeString(TIL_BinaryOpcode Op);



/// Macro for the ugly template return type of SExpr::traverse
#define MAPTYPE(R, X) typename R::template TypeMap<X>::Ty

#define DECLARE_TRAVERSE_AND_COMPARE(X)                       \
  template <class V>                                          \
  MAPTYPE(V::RMap, X) traverse(V &Vs);                        \
                                                              \
  template <class C>                                          \
  typename C::CType compare(const X* E, C& Cmp) const;



class BasicBlock;
class Instruction;

/// Base class for AST nodes in the typed intermediate language.
class SExpr {
public:
  TIL_Opcode opcode() const { return static_cast<TIL_Opcode>(Opcode); }

  /// Return true if this is a trivial SExpr (constant or variable name).
  bool isTrivial() {
    switch (Opcode) {
      case COP_ScalarType: return true;
      case COP_Literal:    return true;
      case COP_Variable:   return true;
      default:             return false;
    }
  }

  /// Cast this SExpr to a CFG instruction, or return null if it is not one.
  Instruction* asCFGInstruction();

  const Instruction* asCFGInstruction() const {
    return const_cast<SExpr*>(this)->asCFGInstruction();
  }

  void *operator new(size_t S, MemRegionRef &R) {
    return ::operator new(S, R);
  }

  /// SExpr objects cannot be deleted.
  // This declaration is public to workaround a gcc bug that breaks building
  // with REQUIRES_EH=1.
  void operator delete(void *) LLVM_DELETED_FUNCTION;

protected:
  SExpr(TIL_Opcode Op, unsigned char SubOp = 0)
    : Opcode(Op), SubOpcode(SubOp), Flags(0) {}
  SExpr(const SExpr &E)
    : Opcode(E.Opcode), SubOpcode(E.SubOpcode), Flags(E.Flags) {}

  const unsigned char Opcode;
  const unsigned char SubOpcode;
  unsigned short Flags;

private:
  SExpr() LLVM_DELETED_FUNCTION;

  /// SExpr objects must be created in an arena.
  void *operator new(size_t) LLVM_DELETED_FUNCTION;
};



/// Instructions are expressions with computational effect that can appear
/// inside basic blocks.
class Instruction : public SExpr {
public:
  static bool classof(const SExpr *E) {
    return E->opcode() >= COP_Literal  &&  E->opcode() <= COP_Future;
  }

  static const unsigned InvalidInstrID = 0xFFFFFFFF;

  Instruction(TIL_Opcode Op, unsigned char SubOp = 0)
      : SExpr(Op, SubOp), ValType(ValueType::getValueType<void>()),
        InstrID(0), StackID(0), Block(nullptr), Name("", 0) { }
  Instruction(const Instruction &E)
      : SExpr(E), ValType(E.ValType),
        InstrID(0), StackID(0), Block(nullptr), Name(E.Name) { }

  /// Return the type of this instruction
  ValueType valueType() const { return ValType; }

  /// Returns the instruction ID for this expression.
  /// All basic block instructions have an ID that is unique within the CFG.
  unsigned instrID() const { return InstrID; }

  /// Returns the depth of this instruction on the stack.
  /// This is used when interpreting a program using a stack machine.
  unsigned stackID() const { return StackID; }

  /// Returns the block, if this is an instruction in a basic block,
  /// otherwise returns null.
  BasicBlock* block() const { return Block; }

  /// Return the name (if any) of this instruction.
  StringRef instrName() const { return Name; }

  /// Set the basic block and instruction ID for this instruction.
  void setInstrID(unsigned id) { InstrID = id; }

  /// Set the basic block for this instruction.
  void setBlock(BasicBlock *B) { Block = B; }

  /// Set the stack ID for this instruction.
  void setStackID(unsigned D) { StackID = D; }

  /// Sets the name of this instructions.
  void setInstrName(StringRef N) { Name = N; }

protected:
  ValueType    ValType;
  unsigned     InstrID;
  unsigned     StackID;
  BasicBlock*  Block;
  StringRef    Name;
};


inline Instruction* SExpr::asCFGInstruction() {
  Instruction* I = dyn_cast<Instruction>(this);
  if (I && I->block() && I->instrID() > 0)
    return I;
  return nullptr;
}



/// Placeholder for an expression that has not yet been created.
/// Used to implement lazy copy and rewriting strategies.
class Future : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Future; }

  enum FutureStatus : unsigned char {
    FS_pending,
    FS_evaluating,
    FS_done
  };

  Future()
      : Instruction(COP_Future), Status(FS_pending),
        Result(nullptr), IPos(nullptr)  { }

public:
  // Return the result of this future if it exists, otherwise return null.
  SExpr *maybeGetResult() const { return Result; }
  FutureStatus status() const { return Status; }
  void setStatus(FutureStatus FS) { Status = FS; }

  // Connect this future to the given position.
  // Forcing the future will overwrite the value at the position.
  SExpr* addPosition(SExpr **Eptr);

  // Connect this future to the given position in a basic block.
  void addPosition(Instruction **Iptr) {
    assert(!IPos);
    IPos = Iptr;
  }

  /// Derived classes must override evaluate to compute the future.
  virtual SExpr* evaluate() = 0;

  /// Return the result, calling evaluate() and setResult() if necessary.
  SExpr* force();

  /// Set the result of this future, and overwrite occurrences with the result.
  void setResult(SExpr *Res);

  DECLARE_TRAVERSE_AND_COMPARE(Future)

private:
  FutureStatus Status;
  SExpr *Result;
  Instruction** IPos;
  std::vector<SExpr**> Positions;
};


inline SExpr* maybeRegisterFuture(SExpr** Eptr, SExpr* P) {
  if (auto *F = dyn_cast_or_null<Future>(P))
    return F->addPosition(Eptr);
  return P;
}

template<class T>
inline T* maybeRegisterFuture(T** Eptr, T* P) {
  // Futures can only be stored in places that can hold any SExpr.
  assert(P->opcode() != COP_Future);
  return P;
}


/// Owning reference to an SExpr.
/// All SExprs should use this class to refer to subexpressions.
template<class T>
class SExprRefT {
public:
  SExprRefT() : Ptr(nullptr) { }
  SExprRefT(std::nullptr_t) : Ptr(nullptr) { }
  SExprRefT(T* P) : Ptr(maybeRegisterFuture(&Ptr, P)) { }

  T&       operator*()        { return *Ptr; }
  const T& operator*() const  { return *Ptr; }
  T*       operator->()       { return Ptr; }
  const T* operator->() const { return Ptr; }

  T*       get()       { return Ptr; }
  const T* get() const { return Ptr; }

  void reset(std::nullptr_t) {
    assert(!Ptr || !isa<Future>(Ptr) && "Reset before future was forced.");
    Ptr = nullptr;
  }

  void reset(T* P) {
    assert(!Ptr || !isa<Future>(Ptr) && "Reset before future was forced.");
    Ptr = maybeRegisterFuture(&Ptr, P);
  }

  bool operator==(const SExprRefT<T> &P) const { return Ptr == P.Ptr; }
  bool operator==(const T* P)            const { return Ptr == P; }
  bool operator==(std::nullptr_t)        const { return Ptr == nullptr; }

  bool operator!=(const SExprRefT<T> &P) const { return Ptr != P.Ptr; }
  bool operator!=(const T* P)            const { return Ptr != P; }
  bool operator!=(std::nullptr_t)        const { return Ptr != nullptr; }

private:
  friend class Future;

  SExprRefT(const SExprRefT<T> &P) : Ptr(P.Ptr) { }
  void operator=(const SExprRefT<T> &P) { }

  T* Ptr;
};

typedef SExprRefT<SExpr> SExprRef;



// Nodes which declare variables
class Function;
class Let;
class Letrec;


/// A declaration for a named variable.
/// There are three ways to introduce a new variable:
///   Let-expressions:           (Let (x = t) u)
///   Functions:                 (Function (x : t) u)
///   Self-applicable functions  (Function (x) t)
class VarDecl : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_VarDecl; }

  enum VariableKind : unsigned char {
    VK_Fun,     ///< Function parameter
    VK_SFun,    ///< Self-applicable Function (self) parameter
    VK_Let,     ///< Let-variable
    VK_Letrec,  ///< Letrec-variable  (mu-operator for recursive definition)
  };

  VarDecl(VariableKind K, StringRef s, SExpr *D)
      : SExpr(COP_VarDecl, K), VarIndex(0), Name(s), Definition(D) { }
  VarDecl(const VarDecl &Vd, SExpr *D)  // rewrite constructor
      : SExpr(Vd), VarIndex(0), Name(Vd.Name), Definition(D) { }

  void rewrite(SExpr *D) { Definition.reset(D); }

  /// Return the kind of variable (let, function param, or self)
  VariableKind kind() const { return static_cast<VariableKind>(SubOpcode); }

  /// Return the de-bruin index of the variable.
  unsigned varIndex() const { return VarIndex; }

  /// Return the name of the variable, if any.
  StringRef name() const { return Name; }

  /// Return the definition of the variable.
  /// For let-vars, this is the setting expression.
  /// For function and self parameters, it is the type of the variable.
  SExpr *definition() { return Definition.get(); }
  const SExpr *definition() const { return Definition.get(); }

  void setName(StringRef S)    { Name = S;  }
  void setVarIndex(unsigned i) { VarIndex = i; }
  void setDefinition(SExpr *E) { Definition.reset(E); }

  DECLARE_TRAVERSE_AND_COMPARE(VarDecl)

private:
  friend class Function;
  friend class Let;
  friend class Letrec;

  unsigned  VarIndex;      // The de-bruin index of the variable.
  StringRef Name;          // The name of the variable.
  SExprRef  Definition;    // The TIL type or definition.
};


/// A function -- a.k.a. lambda abstraction.
/// Functions with multiple arguments are created by currying,
/// e.g. (Function (x: Int) (Function (y: Int) (Code { return x + y; })))
class Function : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Function; }

  Function(VarDecl *Vd, SExpr *Bd)
      : SExpr(COP_Function), VDecl(Vd), Body(Bd) {
    assert(Vd->kind() == VarDecl::VK_Fun || Vd->kind() == VarDecl::VK_SFun);
    if (Vd->kind() == VarDecl::VK_SFun) {
      assert(Vd->definition() == nullptr);
      Vd->Definition.reset(this);
    }
  }
  Function(const Function &F, VarDecl *Vd, SExpr *Bd) // rewrite constructor
      : SExpr(F), VDecl(Vd), Body(Bd) {
    assert(Vd->kind() == VarDecl::VK_Fun || Vd->kind() == VarDecl::VK_SFun);
    if (Vd->kind() == VarDecl::VK_SFun) {
      assert(Vd->definition() == nullptr);
      Vd->Definition.reset(this);
    }
  }

  void rewrite(VarDecl *Vd, SExpr *Bd) {
    VDecl.reset(Vd);
    Body.reset(Bd);
  }

  VarDecl *variableDecl()  { return VDecl.get(); }
  const VarDecl *variableDecl() const { return VDecl.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Function)

private:
  SExprRefT<VarDecl> VDecl;
  SExprRef           Body;
};



/// A block of code -- e.g. the body of a function.
class Code : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Code; }

  Code(SExpr *T, SExpr *B) : SExpr(COP_Code), ReturnType(T), Body(B) {}
  Code(const Code &C, SExpr *T, SExpr *B) // rewrite constructor
      : SExpr(C), ReturnType(T), Body(B) {}

  void rewrite(SExpr *T, SExpr *B) {
    ReturnType.reset(T);
    Body.reset(B);
  }

  SExpr *returnType() { return ReturnType.get(); }
  const SExpr *returnType() const { return ReturnType.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Code)

private:
  SExprRef ReturnType;
  SExprRef Body;
};


/// A typed, writable location in memory
class Field : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Field; }

  Field(SExpr *R, SExpr *B) : SExpr(COP_Field), Range(R), Body(B) {}
  Field(const Field &C, SExpr *R, SExpr *B) // rewrite constructor
      : SExpr(C), Range(R), Body(B) {}

  void rewrite(SExpr *R, SExpr *B) {
    Range.reset(R);
    Body.reset(B);
  }

  SExpr *range() { return Range.get(); }
  const SExpr *range() const { return Range.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Field)

private:
  SExprRef Range;
  SExprRef Body;
};


/// A Slot (i.e. a named definition) in a Record.
class Slot : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Slot; }

  enum SlotKind : unsigned short {
    SLT_Normal   = 0,
    SLT_Final    = 1,
    SLT_Override = 2
  };

  Slot(StringRef N, SExpr *D) : SExpr(COP_Slot), Name(N), Definition(D) { }
  Slot(const Slot &S, SExpr *D) : SExpr(S), Name(S.Name), Definition(D) { }

  void rewrite(SExpr *D) { Definition.reset(D); }

  StringRef name() const { return Name; }

  SExpr *definition() { return Definition.get(); }
  const SExpr *definition() const { return Definition.get(); }

  unsigned hasModifier  (SlotKind K) { return (Flags & K) != 0; }
  void     setModifier  (SlotKind K) { Flags = Flags | K;  }
  void     clearModifier(SlotKind K) { Flags = Flags & ~K; }

  DECLARE_TRAVERSE_AND_COMPARE(Slot)

private:
  StringRef Name;
  SExprRef  Definition;
};


/// A record, which is similar to a C struct.
/// A record is essentially a function from slot names to definitions.
class Record : public SExpr {
public:
  typedef ArrayTree<SExprRefT<Slot>> SlotArray;
  typedef DenseMap<std::string, unsigned> SlotMap;

  static bool classof(const SExpr *E) { return E->opcode() == COP_Record; }

  Record(MemRegionRef A, unsigned NSlots)
    : SExpr(COP_Record), Slots(A, NSlots), SMap(nullptr) {}
  Record(const Record &R, MemRegionRef A)
    : SExpr(R), Slots(A, R.Slots.size()), SMap(R.SMap) {}

  SlotArray& slots() { return Slots; }
  const SlotArray& slots() const { return Slots; }

  Slot* findSlot(StringRef S);

  DECLARE_TRAVERSE_AND_COMPARE(Record)

private:
  SlotArray Slots;    //< The slots in the record.
  SlotMap*  SMap;     //< A map from slot names to indices.
};



/// Simple scalar types, e.g. Int, Float, etc.
class ScalarType : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_ScalarType; }

  ScalarType(ValueType VT) : SExpr(COP_ScalarType), ValType(VT)  { }
  ScalarType(const ScalarType &S) : SExpr(S), ValType(S.ValType) { }

  /// Return the type of this instruction
  ValueType valueType() const { return ValType; }

  DECLARE_TRAVERSE_AND_COMPARE(ScalarType)

private:
  ValueType ValType;
};



template <class T> class LiteralT;

/// Base class for literal values.
class Literal : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Literal; }

  Literal(ValueType VT) : Instruction(COP_Literal) { ValType = VT; }
  Literal(const Literal &L) : Instruction(L) { }

  template<class T> const LiteralT<T>& as() const {
    return *static_cast<const LiteralT<T>*>(this);
  }
  template<class T> LiteralT<T>& as() {
    return *static_cast<LiteralT<T>*>(this);
  }

  DECLARE_TRAVERSE_AND_COMPARE(Literal)
};


/// Derived class for literal values, which stores the actual value.
template<class T>
class LiteralT : public Literal {
public:
  LiteralT(T Dat) : Literal(ValueType::getValueType<T>()), Val(Dat) { }
  LiteralT(const LiteralT<T> &L) : Literal(L), Val(L.Val) { }

  T  value() const { return Val;}
  T& value() { return Val; }

private:
  T Val;
};


/// A variable, which refers to a previously declared VarDecl.
class Variable : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Variable; }

  Variable(VarDecl *VD)
     : Instruction(COP_Variable), VDecl(VD) { }
  Variable(const Variable &V, VarDecl *VD)
     : Instruction(V), VDecl(VD) { }

  void rewrite(VarDecl *D) { VDecl.reset(D); }

  const VarDecl* variableDecl() const { return VDecl.get(); }
  VarDecl* variableDecl() { return VDecl.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Variable)

private:
  SExprRefT<VarDecl> VDecl;
};


/// Apply an argument to a function.
/// Note that this does not actually call the function.  Functions are curried,
/// so this returns a closure in which the first parameter has been applied.
/// Once all parameters have been applied, Call can be used to invoke the
/// function.
class Apply : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Apply; }

  enum FunctionApplyKind : unsigned char {
    FAK_Apply = 0,   // Application of a normal function
    FAK_SApply       // Self-application
  };

  Apply(SExpr *F, SExpr *A, FunctionApplyKind K = FAK_Apply)
      : Instruction(COP_Apply, K), Fun(F), Arg(A)
  {}
  Apply(const Apply &A, SExpr *F, SExpr *Ar)  // rewrite constructor
      : Instruction(A), Fun(F), Arg(Ar)
  {}

  void rewrite(SExpr *F, SExpr *A) {
    Fun.reset(F);
    Arg.reset(A);
  }

  bool isSelfApplication() const { return SubOpcode == FAK_SApply; }
  bool isDelegation() const { return isSelfApplication() && Arg != nullptr; }

  SExpr *fun() { return Fun.get(); }
  const SExpr *fun() const { return Fun.get(); }

  SExpr *arg() { return Arg.get(); }
  const SExpr *arg() const { return Arg.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Apply)

private:
  SExprRef Fun;
  SExprRef Arg;
};


/// Project a named slot from a record.  (Struct or class.)
class Project : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Project; }

  static const short PRJ_Arrow = 0x01;

  Project(SExpr *R, StringRef SName)
      : Instruction(COP_Project), Rec(R), SlotName(SName), Cvdecl(nullptr) { }
  Project(SExpr *R, const clang::ValueDecl *Cvd)
      : Instruction(COP_Project), Rec(R), SlotName(Cvd->getName()), Cvdecl(Cvd)
  { }
  Project(const Project &P, SExpr *R)
      : Instruction(P), Rec(R), SlotName(P.SlotName), Cvdecl(P.Cvdecl) { }

  void rewrite(SExpr *R) { Rec.reset(R); }

  SExpr *record() { return Rec.get(); }
  const SExpr *record() const { return Rec.get(); }

  const clang::ValueDecl *clangDecl() const { return Cvdecl; }

  bool isArrow() const { return (Flags & PRJ_Arrow) != 0; }
  void setArrow(bool b) {
    if (b) Flags |= PRJ_Arrow;
    else Flags &= ~PRJ_Arrow;
  }

  StringRef slotName() const {
    if (Cvdecl)
      return Cvdecl->getName();
    else
      return SlotName;
  }

  DECLARE_TRAVERSE_AND_COMPARE(Project)

private:
  SExprRef Rec;
  StringRef SlotName;
  const clang::ValueDecl *Cvdecl;
};


/// Call a function (after all arguments have been applied).
class Call : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Call; }

  Call(SExpr *T) : Instruction(COP_Call), Target(T) { }
  Call(const Call &C, SExpr *T) : Instruction(C), Target(T) { }

  void rewrite(SExpr *T) { Target.reset(T); }

  SExpr *target() { return Target.get(); }
  const SExpr *target() const { return Target.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Call)

private:
  SExprRef Target;
};


/// Allocate memory for a new value on the heap or stack.
class Alloc : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Alloc; }

  enum AllocKind : unsigned char {
    AK_Local,  // Local variable, which must get lowered to SSA.
    AK_Stack,  // Stack-allocated structure, which may get lowered to SSA.
    AK_Heap    // Heap-allocated structure
  };

  Alloc(SExpr *E, AllocKind K) : Instruction(COP_Alloc, K), InitExpr(E) { }
  Alloc(const Alloc &A, SExpr *E) : Instruction(A), InitExpr(E) { }

  void rewrite(SExpr *I) { InitExpr.reset(I); }

  AllocKind kind() const { return static_cast<AllocKind>(SubOpcode); }

  SExpr *initializer() { return InitExpr.get(); }
  const SExpr *initializer() const { return InitExpr.get(); }

  // For an alloca, return an index into a virtual stack.
  // Used for SSA renaming and abstract interpretation.
  unsigned allocID() const { return AllocID; }

  void setAllocID(unsigned I) { AllocID = I; }

  DECLARE_TRAVERSE_AND_COMPARE(Alloc)

private:
  SExprRef InitExpr;
  unsigned AllocID;
};


/// Load a value from memory.
class Load : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Load; }

  Load(SExpr *P) : Instruction(COP_Load), Ptr(P) {}
  Load(const Load &L, SExpr *P) : Instruction(L), Ptr(P) {}

  void rewrite(SExpr *P) { Ptr.reset(P); }

  SExpr *pointer() { return Ptr.get(); }
  const SExpr *pointer() const { return Ptr.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Load)

private:
  SExprRef Ptr;
};


/// Store a value to memory.
/// The destination is a pointer to a field, the source is the value to store.
class Store : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Store; }

  Store(SExpr *P, SExpr *V)
      : Instruction(COP_Store), Dest(P), Source(V) {}
  Store(const Store &S, SExpr *P, SExpr *V)
      : Instruction(S), Dest(P), Source(V) {}

  void rewrite(SExpr *D, SExpr *S) {
    Dest.reset(D);
    Source.reset(S);
  }

  SExpr *destination() { return Dest.get(); }  // Address to store to
  const SExpr *destination() const { return Dest.get(); }

  SExpr *source() { return Source.get(); }     // Value to store
  const SExpr *source() const { return Source.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Store)

private:
  SExprRef Dest;
  SExprRef Source;
};


/// If p is a reference to an array, then p[i] is a reference to the i'th
/// element of the array.
class ArrayIndex : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_ArrayIndex; }

  ArrayIndex(SExpr *A, SExpr *N)
      : Instruction(COP_ArrayIndex), Array(A), Index(N) {}
  ArrayIndex(const ArrayIndex &E, SExpr *A, SExpr *N)
      : Instruction(E), Array(A), Index(N) {}

  void rewrite(SExpr *A, SExpr *I) {
    Array.reset(A);
    Index.reset(I);
  }

  SExpr *array() { return Array.get(); }
  const SExpr *array() const { return Array.get(); }

  SExpr *index() { return Index.get(); }
  const SExpr *index() const { return Index.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(ArrayIndex)

private:
  SExprRef Array;
  SExprRef Index;
};


/// Pointer arithmetic, restricted to arrays only.
/// If p is a reference to an array, then p + n, where n is an integer, is
/// a reference to a subarray.
class ArrayAdd : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_ArrayAdd; }

  ArrayAdd(SExpr *A, SExpr *N)
      : Instruction(COP_ArrayAdd), Array(A), Index(N) {}
  ArrayAdd(const ArrayAdd &E, SExpr *A, SExpr *N)
      : Instruction(E), Array(A), Index(N) {}

  void rewrite(SExpr *A, SExpr *I) {
    Array.reset(A);
    Index.reset(I);
  }

  SExpr *array() { return Array.get(); }
  const SExpr *array() const { return Array.get(); }

  SExpr *index() { return Index.get(); }
  const SExpr *index() const { return Index.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(ArrayAdd)

private:
  SExprRef Array;
  SExprRef Index;
};


/// Simple arithmetic unary operations, e.g. negate and not.
/// These operations have no side-effects.
class UnaryOp : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_UnaryOp; }

  UnaryOp(TIL_UnaryOpcode Op, SExpr *E)
      : Instruction(COP_UnaryOp, Op), Expr0(E) { }
  UnaryOp(const UnaryOp &U, SExpr *E) : Instruction(U), Expr0(E) { }

  void rewrite(SExpr *E) { Expr0.reset(E); }

  TIL_UnaryOpcode unaryOpcode() const {
    return static_cast<TIL_UnaryOpcode>(SubOpcode);
  }

  SExpr *expr() { return Expr0.get(); }
  const SExpr *expr() const { return Expr0.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(UnaryOp)

private:
  SExprRef Expr0;
};


/// Simple arithmetic binary operations, e.g. +, -, etc.
/// These operations have no side effects.
class BinaryOp : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_BinaryOp; }

  BinaryOp(TIL_BinaryOpcode Op, SExpr *E0, SExpr *E1)
      : Instruction(COP_BinaryOp, Op), Expr0(E0), Expr1(E1) { }
  BinaryOp(const BinaryOp &B, SExpr *E0, SExpr *E1)
      : Instruction(B), Expr0(E0), Expr1(E1) { }

  void rewrite(SExpr *E0, SExpr *E1) {
    Expr0.reset(E0);
    Expr1.reset(E1);
  }

  TIL_BinaryOpcode binaryOpcode() const {
    return static_cast<TIL_BinaryOpcode>(SubOpcode);
  }

  SExpr *expr0() { return Expr0.get(); }
  const SExpr *expr0() const { return Expr0.get(); }

  SExpr *expr1() { return Expr1.get(); }
  const SExpr *expr1() const { return Expr1.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(BinaryOp)

private:
  SExprRef Expr0;
  SExprRef Expr1;
};


/// Cast expressions.
/// Cast expressions are essentially unary operations, but we treat them
/// as a distinct AST node because they only change the type of the result.
class Cast : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Cast; }

  Cast(TIL_CastOpcode Op, SExpr *E)
      : Instruction(COP_Cast, Op), Expr0(E) { }
  Cast(const Cast &C, SExpr *E) : Instruction(C), Expr0(E) { }

  void rewrite(SExpr *E) { Expr0.reset(E); }

  TIL_CastOpcode castOpcode() const {
    return static_cast<TIL_CastOpcode>(SubOpcode);
  }

  SExpr *expr() { return Expr0.get(); }
  const SExpr *expr() const { return Expr0.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Cast)

private:
  SExprRef Expr0;
};


class SCFG;


/// Phi Node, for code in SSA form.
/// Each Phi node has an array of possible values that it can take,
/// depending on where control flow comes from.
class Phi : public Instruction {
public:
  typedef ArrayTree<SExprRef, 2> ValArray;

  // In minimal SSA form, all Phi nodes are MultiVal.
  // During conversion to SSA, incomplete Phi nodes may be introduced, which
  // are later determined to be SingleVal, and are thus redundant.
  enum Status : unsigned short {
    PH_MultiVal = 0, // Phi node has multiple distinct values.  (Normal)
    PH_SingleVal,    // Phi node has one distinct value, and can be eliminated
    PH_Incomplete    // Phi node is incomplete
  };

  static bool classof(const SExpr *E) { return E->opcode() == COP_Phi; }

  Phi() : Instruction(COP_Phi) { }
  Phi(MemRegionRef A, unsigned Nvals, Alloc* Lv = nullptr)
      : Instruction(COP_Phi), Values(A, Nvals) { }
  Phi(const Phi &Ph, MemRegionRef A, Alloc* Lv = nullptr)
      : Instruction(Ph), Values(A, Ph.values().size()) { }

  /// Return the array of Phi arguments
  const ValArray &values() const { return Values; }
  ValArray &values() { return Values; }

  Status status() const { return static_cast<Status>(Flags); }
  void setStatus(Status s) { Flags = s; }

  DECLARE_TRAVERSE_AND_COMPARE(Phi)

private:
  ValArray Values;
};


/// Base class for basic block terminators:  Branch, Goto, and Return.
class Terminator : public Instruction {
public:
  static bool classof(const SExpr *E) {
    return E->opcode() >= COP_Goto && E->opcode() <= COP_Return;
  }

  typedef ArrayRef<SExprRefT<BasicBlock>> BlockArray;

protected:
  Terminator(TIL_Opcode Op) : Instruction(Op) { }
  Terminator(const Instruction &E) : Instruction(E) { }

public:
  BlockArray successors();

  /// Return the list of basic blocks that this terminator can branch to.
  BlockArray successors() const {
    return const_cast<Terminator*>(this)->successors();
  }
};


/// Jump to another basic block.
/// A goto instruction is essentially a tail-recursive call into another
/// block.  In addition to the block pointer, it specifies an index into the
/// phi nodes of that block.  The index can be used to retrieve the "arguments"
/// of the call.
class Goto : public Terminator {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Goto; }

  Goto(BasicBlock *B, unsigned I)
      : Terminator(COP_Goto), TargetBlock(B), Index(I) {}
  Goto(const Goto &G, BasicBlock *B, unsigned I)
      : Terminator(COP_Goto), TargetBlock(B), Index(I) {}

  void rewrite(BasicBlock* B, unsigned Idx) {
    TargetBlock.reset(B);
    Index = Idx;
  }

  const BasicBlock *targetBlock() const { return TargetBlock.get(); }
  BasicBlock *targetBlock() { return TargetBlock.get(); }

  /// Returns the argument index into the Phi nodes for this branch.
  unsigned phiIndex() const { return Index; }

  bool isBackEdge() const;

  /// Return the list of basic blocks that this terminator can branch to.
  BlockArray successors() { return BlockArray(&TargetBlock, 1); }

  DECLARE_TRAVERSE_AND_COMPARE(Goto)

private:
  SExprRefT<BasicBlock> TargetBlock;
  unsigned Index;
};


/// A conditional branch to two other blocks.
/// Note that unlike Goto, Branch does not have an index.  The target blocks
/// must be child-blocks, and cannot have Phi nodes.
class Branch : public Terminator {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Branch; }

  Branch(SExpr *C, BasicBlock *T, BasicBlock *E)
      : Terminator(COP_Branch), Condition(C) {
    Branches[0].reset(T);
    Branches[1].reset(E);
  }
  Branch(const Branch &Br, SExpr *C, BasicBlock *T, BasicBlock *E)
      : Terminator(Br), Condition(C) {
    Branches[0].reset(T);
    Branches[1].reset(E);
  }

  void rewrite(SExpr *C, BasicBlock *B1, BasicBlock *B2) {
    Condition.reset(C);
    Branches[0].reset(B1);
    Branches[1].reset(B2);
  }

  const SExpr *condition() const { return Condition.get(); }
  SExpr *condition() { return Condition.get(); }

  const BasicBlock *thenBlock() const { return Branches[0].get(); }
  BasicBlock *thenBlock() { return Branches[0].get(); }

  const BasicBlock *elseBlock() const { return Branches[1].get(); }
  BasicBlock *elseBlock() { return Branches[1].get(); }

  /// Return the list of basic blocks that this terminator can branch to.
  BlockArray successors() { return BlockArray(Branches, 2); }

  DECLARE_TRAVERSE_AND_COMPARE(Branch)

private:
  SExprRef              Condition;
  SExprRefT<BasicBlock> Branches[2];
};


/// Return from the enclosing function, passing the return value to the caller.
/// Only the exit block should end with a return statement.
class Return : public Terminator {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Return; }

  Return(SExpr* Rval) : Terminator(COP_Return), Retval(Rval) {}
  Return(const Return &R, SExpr* Rval) : Terminator(R), Retval(Rval) {}

  void rewrite(SExpr *R) { Retval.reset(R); }

  /// Return an empty list.
  BlockArray successors() { return BlockArray(); }

  SExpr *returnValue() { return Retval.get(); }
  const SExpr *returnValue() const { return Retval.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Return)

private:
  SExprRef Retval;
};


inline Terminator::BlockArray Terminator::successors() {
  switch (opcode()) {
    case COP_Goto:   return cast<Goto>(this)->successors();
    case COP_Branch: return cast<Branch>(this)->successors();
    case COP_Return: return cast<Return>(this)->successors();
    default:
      return BlockArray();
  }
}


/// A basic block is part of an SCFG.  It can be treated as a function in
/// continuation passing style.  A block consists of a sequence of phi nodes,
/// which are "arguments" to the function, followed by a sequence of
/// instructions.  It ends with a Terminator, which is a Branch or Goto to
/// another basic block in the same SCFG.
class BasicBlock : public SExpr {
public:
  typedef ArrayTree<Phi*>                      ArgArray;
  typedef ArrayTree<Instruction*>              InstrArray;
  typedef ArrayTree<SExprRefT<BasicBlock>, 2>  PredArray;
  typedef ArrayTree<SExprRefT<BasicBlock>>     BlockArray;

  static const unsigned InvalidBlockID = 0x0FFFFFFF;

  // TopologyNodes are used to overlay tree structures on top of the CFG,
  // such as dominator and postdominator trees.  Each block is assigned an
  // ID in the tree according to a depth-first search.  Tree traversals are
  // always up, towards the parents.
  struct TopologyNode {
    TopologyNode() : NodeID(0), SizeOfSubTree(0), Parent(nullptr) {}

    bool isParentOf(const TopologyNode& OtherNode) {
      return OtherNode.NodeID > NodeID &&
             OtherNode.NodeID < NodeID + SizeOfSubTree;
    }

    bool isParentOfOrEqual(const TopologyNode& OtherNode) {
      return OtherNode.NodeID >= NodeID &&
             OtherNode.NodeID < NodeID + SizeOfSubTree;
    }

    int NodeID;
    int SizeOfSubTree;    // Includes this node, so must be > 1.
    BasicBlock *Parent;   // Pointer to parent.
  };

  static bool classof(const SExpr *E) { return E->opcode() == COP_BasicBlock; }

  /// Returns the block ID.  Every block has a unique ID in the CFG.
  size_t blockID() const { return BlockID; }
  void setBlockID(size_t i) { BlockID = i; }

  /// Returns the number of predecessors.
  size_t numPredecessors() const { return Predecessors.size(); }
  size_t numSuccessors()   const { return successors().size(); }

  const SCFG* cfg() const { return CFGPtr; }
  SCFG* cfg() { return CFGPtr; }

  const BasicBlock *parent() const { return DominatorNode.Parent; }
  BasicBlock *parent() { return DominatorNode.Parent; }
  const BasicBlock *postDominator() const { return PostDominatorNode.Parent; }
  BasicBlock *postDominator() { return PostDominatorNode.Parent; }

  const ArgArray &arguments() const { return Args; }
  ArgArray &arguments() { return Args; }

  InstrArray &instructions() { return Instrs; }
  const InstrArray &instructions() const { return Instrs; }

  /// Returns a list of predecessors.
  /// The order of predecessors in the list is important; each phi node has
  /// exactly one argument for each precessor, in the same order.
  PredArray &predecessors() { return Predecessors; }
  const PredArray &predecessors() const { return Predecessors; }

  Terminator::BlockArray successors() {
    return TermInstr ? TermInstr->successors() : Terminator::BlockArray();
  }
  Terminator::BlockArray successors() const {
    return TermInstr ? TermInstr->successors() : Terminator::BlockArray();
  }

  const Terminator *terminator() const { return TermInstr; }
  Terminator *terminator() { return TermInstr; }

  unsigned depth() const { return Depth; }
  void setDepth(unsigned D) { Depth = D; }

  unsigned loopDepth() const { return LoopDepth; }
  void setLoopDepth(unsigned Ld) { LoopDepth = Ld; }

  bool dominates(const BasicBlock &Other) {
    return DominatorNode.isParentOfOrEqual(Other.DominatorNode);
  }

  bool postDominates(const BasicBlock &Other) {
    return PostDominatorNode.isParentOfOrEqual(Other.PostDominatorNode);
  }

  /// Add a new argument.
  void addArgument(Phi *E) {
    Args.emplace_back(Arena, E);
    E->setBlock(this);
  }

  /// Add a new instruction.
  void addInstruction(Instruction *E) {
    Instrs.emplace_back(Arena, E);
    E->setBlock(this);
    if (auto *F = dyn_cast<Future>(E))
      F->addPosition(&Instrs.back());
  }

  void setTerminator(Terminator *E) {
    TermInstr = E;
    if (E)
      E->setBlock(this);
  }

  // Add a new predecessor, and return the phi-node index for it.
  // Will add an argument to all phi-nodes, initialized to nullptr.
  unsigned addPredecessor(BasicBlock *Pred);

  // Reserve space for Nargs arguments.
  void reserveArguments(unsigned Nargs) { Args.reserve(Arena, Nargs); }

  // Reserve space for Nins instructions.
  void reserveInstructions(unsigned Nins) { Instrs.reserve(Arena, Nins); }

  // Reserve space for NumPreds predecessors, including space in phi nodes.
  void reservePredecessors(unsigned NumPreds);

  /// Return the index of BB, or Predecessors.size if BB is not a predecessor.
  unsigned findPredecessorIndex(const BasicBlock *BB) const;

  DECLARE_TRAVERSE_AND_COMPARE(BasicBlock)

  explicit BasicBlock(MemRegionRef A)
      : SExpr(COP_BasicBlock), Arena(A), CFGPtr(nullptr), BlockID(0),
        TermInstr(nullptr),
        PostBlockID(0), Depth(0), LoopDepth(0), Visited(false) { }
  BasicBlock(BasicBlock &B, MemRegionRef A)
      : SExpr(B), Arena(A), CFGPtr(nullptr), BlockID(0),
        Args(A, B.Args.size()), Instrs(A, B.Instrs.size()), TermInstr(nullptr),
        PostBlockID(0), Depth(0), LoopDepth(0), Visited(false) { }

private:
  friend class SCFG;

  unsigned renumber(unsigned id);   // assign unique ids to all instructions
  int  topologicalSort     (BlockArray& Blocks, int ID);
  int  postTopologicalSort (BlockArray& Blocks, int ID);
  void computeDominator();
  void computePostDominator();

private:
  MemRegionRef Arena;        // The arena used to allocate this block.
  SCFG         *CFGPtr;      // The CFG that contains this block.
  unsigned     BlockID;      // unique id for this BB in the containing CFG.
                             // IDs are in topological order.
  PredArray   Predecessors;  // Predecessor blocks in the CFG.
  ArgArray    Args;          // Phi nodes.  One argument per predecessor.
  InstrArray  Instrs;        // Instructions.
  Terminator* TermInstr;     // Terminating instruction

  unsigned     PostBlockID;  // ID in post-topological order
  unsigned     Depth;        // The instruction Depth of the first instruction.
  unsigned     LoopDepth;    // The level of nesting within loops.
  bool         Visited;      // Bit to determine if a block has been visited
                             // during a traversal.

  TopologyNode DominatorNode;       // The dominator tree
  TopologyNode PostDominatorNode;   // The post-dominator tree
};


/// An SCFG is a control-flow graph.  It consists of a set of basic blocks,
/// each of which terminates in a branch to another basic block.  There is one
/// entry point, and one exit point.
class SCFG : public SExpr {
public:
  typedef BasicBlock::BlockArray     BlockArray;
  typedef BlockArray::iterator       iterator;
  typedef BlockArray::const_iterator const_iterator;

  static bool classof(const SExpr *E) { return E->opcode() == COP_SCFG; }

  SCFG(MemRegionRef A, unsigned Nblocks)
    : SExpr(COP_SCFG), Arena(A), Blocks(A, Nblocks),
      Entry(nullptr), Exit(nullptr), NumInstructions(0), Normal(false) {
    Entry = new (A) BasicBlock(A);
    Exit  = new (A) BasicBlock(A);
    auto *V = new (A) Phi();
    Exit->addArgument(V);
    Exit->setTerminator(new (A) Return(V));
    add(Entry);
    add(Exit);
    Exit->setBlockID(1);
  }
  SCFG(const SCFG &Cfg, MemRegionRef A)
    : SExpr(COP_SCFG), Arena(A), Blocks(A, Cfg.numBlocks()),
      Entry(nullptr), Exit(nullptr), NumInstructions(0), Normal(false) { }

  /// Return true if this CFG is valid.
  bool valid() const { return Entry && Exit && Blocks.size() > 0; }

  /// Return true if this CFG has been normalized.
  /// After normalization, blocks are in topological order, and block and
  /// instruction IDs have been assigned.
  bool normal() const { return Normal; }

  const BlockArray& blocks() const { return Blocks; }
  BlockArray&       blocks()       { return Blocks; }

  const BasicBlock *entry() const { return Entry; }
  BasicBlock       *entry()       { return Entry; }
  const BasicBlock *exit()  const { return Exit; }
  BasicBlock       *exit()        { return Exit; }

  /// Return the number of blocks in the CFG.
  /// Block::blockID() will return a number less than numBlocks();
  unsigned numBlocks() const { return static_cast<unsigned>(Blocks.size()); }

  /// Return the total number of instructions in the CFG.
  /// This is useful for building instruction side-tables;
  /// A call to SExpr::id() will return a number less than numInstructions().
  unsigned numInstructions() { return NumInstructions; }

  inline void add(BasicBlock *BB) {
    assert(BB->CFGPtr == nullptr);
    BB->CFGPtr = this;
    Blocks.emplace_back(Arena, BB);
  }

  void setEntry(BasicBlock *BB) { Entry = BB; }
  void setExit(BasicBlock *BB)  { Exit = BB;  }

  void renumber();         // assign unique ids to all instructions and blocks
  void computeNormalForm();

  DECLARE_TRAVERSE_AND_COMPARE(SCFG)

private:
  MemRegionRef Arena;
  BlockArray   Blocks;
  BasicBlock   *Entry;
  BasicBlock   *Exit;
  unsigned     NumInstructions;
  bool         Normal;
};



/// Placeholder for expressions that cannot be represented in the TIL.
class Undefined : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Undefined; }

  Undefined() : SExpr(COP_Undefined) { }
  Undefined(const Undefined &U) : SExpr(U) { }

  DECLARE_TRAVERSE_AND_COMPARE(Undefined)
};


/// Placeholder for a wildcard that matches any other expression.
class Wildcard : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Wildcard; }

  Wildcard() : SExpr(COP_Wildcard) {}
  Wildcard(const Wildcard &W) : SExpr(W) {}

  DECLARE_TRAVERSE_AND_COMPARE(Wildcard)
};


/// An identifier, e.g. 'foo' or 'x'.
/// This is a pseduo-term; it will be lowered to a variable or projection.
class Identifier : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Identifier; }

  Identifier(StringRef Id): SExpr(COP_Identifier), Name(Id) { }
  Identifier(const Identifier& I) : SExpr(I), Name(I.Name)  { }

  StringRef name() const { return Name; }

  DECLARE_TRAVERSE_AND_COMPARE(Identifier)

private:
  StringRef Name;
};


/// A let-expression,  e.g.  let x=t; u.
/// This is a pseduo-term; it will be lowered to instructions in a CFG.
class Let : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Let; }

  Let(VarDecl *Vd, SExpr *Bd) : SExpr(COP_Let), VDecl(Vd), Body(Bd) {
    assert(Vd->kind() == VarDecl::VK_Let);
  }
  Let(const Let &L, VarDecl *Vd, SExpr *Bd) : SExpr(L), VDecl(Vd), Body(Bd) {
     assert(Vd->kind() == VarDecl::VK_Let);
  }

  void rewrite(VarDecl *Vd, SExpr *B) {
    VDecl.reset(Vd);
    Body.reset(B);
  }

  VarDecl *variableDecl()  { return VDecl.get(); }
  const VarDecl *variableDecl() const { return VDecl.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Let)

private:
  SExprRefT<VarDecl> VDecl;
  SExprRef Body;
};


/// A let-expression,  e.g.  let x=t; u.
/// This is a pseduo-term; it will be lowered to instructions in a CFG.
class Letrec : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Letrec; }

  Letrec(VarDecl *Vd, SExpr *Bd) : SExpr(COP_Letrec), VDecl(Vd), Body(Bd) {
     assert(Vd->kind() == VarDecl::VK_Letrec);
  }
  Letrec(const Letrec &Lr, VarDecl *Vd, SExpr *Bd)
      : SExpr(Lr), VDecl(Vd), Body(Bd) {
     assert(Vd->kind() == VarDecl::VK_Letrec);
  }

  void rewrite(VarDecl *Vd, SExpr *B) {
    VDecl.reset(Vd);
    Body.reset(B);
  }

  VarDecl *variableDecl()  { return VDecl.get(); }
  const VarDecl *variableDecl() const { return VDecl.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(Letrec)

private:
  SExprRefT<VarDecl> VDecl;
  SExprRef Body;
};



/// An if-then-else expression.
/// This is a pseduo-term; it will be lowered to a branch in a CFG.
class IfThenElse : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_IfThenElse; }

  IfThenElse(SExpr *C, SExpr *T, SExpr *E)
    : SExpr(COP_IfThenElse), Condition(C), ThenExpr(T), ElseExpr(E)
  { }
  IfThenElse(const IfThenElse &I, SExpr *C, SExpr *T, SExpr *E)
    : SExpr(I), Condition(C), ThenExpr(T), ElseExpr(E)
  { }

  void rewrite(SExpr *C, SExpr *E0, SExpr* E1) {
    Condition.reset(C);
    ThenExpr.reset(E0);
    ElseExpr.reset(E1);
  }

  SExpr *condition() { return Condition.get(); }   // Address to store to
  const SExpr *condition() const { return Condition.get(); }

  SExpr *thenExpr() { return ThenExpr.get(); }     // Value to store
  const SExpr *thenExpr() const { return ThenExpr.get(); }

  SExpr *elseExpr() { return ElseExpr.get(); }     // Value to store
  const SExpr *elseExpr() const { return ElseExpr.get(); }

  DECLARE_TRAVERSE_AND_COMPARE(IfThenElse)

private:
  SExprRef Condition;
  SExprRef ThenExpr;
  SExprRef ElseExpr;
};


inline bool Goto::isBackEdge() const {
  return TargetBlock->blockID() <= block()->blockID();
}



}  // end namespace til
}  // end namespace threadSafety
}  // end namespace clang

#endif
