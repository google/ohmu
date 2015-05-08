//===- TIL.h ---------------------------------------------------*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License.  See LICENSE.TXT in the LLVM repository for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a simple Typed Intermediate Language, or TIL, that is used
// by the thread safety analysis (See ThreadSafety.cpp).  The TIL is intended
// to be largely independent of clang, in the hope that the analysis can be
// reused for other non-C++ languages.  All dependencies on clang/llvm should
// go in LLVMDependencies.h.
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

#ifndef OHMU_TIL_TIL_H
#define OHMU_TIL_TIL_H

// Note: we use a relative path for includes, so that these files can be
// reused in a different directory structure outside of clang/llvm.
// All clang and llvm dependencies should go in base/LLVMDependencies.h.

#include "base/ArrayTree.h"
#include "base/LLVMDependencies.h"
#include "base/MemRegion.h"
#include "base/MutArrayRef.h"
#include "base/SimpleArray.h"

#include "Annotation.h"
#include "TILBaseType.h"

#include <stdint.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>


namespace ohmu {
namespace til {

// Forward declaration.
class CFGBuilder;

/// Enum for the different distinct classes of SExpr
enum TIL_Opcode {
#define TIL_OPCODE_DEF(X) COP_##X,
#include "TILOps.def"
};

/// Opcode for unary arithmetic operations.
enum TIL_UnaryOpcode : unsigned char {
  UOP_Negative,     ///<  -
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
  BOP_Gt,           ///<  >   (surface syntax only: will be rewritten to <)
  BOP_Geq,          ///<  >=  (surface syntax only: will be rewritten to <=)
  BOP_LogicAnd,     ///<  &&  (no short-circuit)
  BOP_LogicOr       ///<  ||  (no short-circuit)
};


/// Opcode for cast operations.  (Currently incomplete)
/// A "cast" is a unary operator that converts from one type to another.
/// There are many different sorts of casts, which can be categorized on
/// several axes:
///
/// (A) Lossless vs. lossy:
///     A lossless cast does not discard bits or reduce precision.
///     E.g. extendNum, extendToFloat, or pointer casts.
///
/// (B) Bitwise equality:
///     Pointer and bit casts have no computational effect; the resulting bit
///     sequence is equal to the original, merely viewed at a different type.
///
/// (C) Semantic equality:
///     Casts between related semantic types (e.g.  extendNum, or downCast)
///     return a value that refers to the same object as the original.
///     Other casts (e.g. toBits) return a result that is semantically
///     unrelated to the original value.
///
enum TIL_CastOpcode : unsigned char {
  CAST_none = 0,
  // numeric casts
  CAST_extendNum,       ///< extend precision of number:  int->int or fp->fp
  CAST_truncNum,        ///< truncate precision of numeric type
  CAST_extendToFloat,   ///< convert integer to larger floating point type
  CAST_truncToFloat,    ///< convert integer to smaller floating point type
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

#define TIL_OPCODE_FIRST(X) \
  const TIL_Opcode COP_Min  = COP_##X;
#define TIL_OPCODE_LAST(X) \
  const TIL_Opcode COP_Max  = COP_##X;
#include "TILOps.def"

const TIL_UnaryOpcode  UOP_Min  = UOP_Negative;
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

/// Return the name of a cast opcode.
StringRef getCastOpcodeString(TIL_CastOpcode Op);

/// If Vt1 can be converted to Vt2 without loss of precision, then return
/// the opcode that does the cast, otherwise return CAST_none.
TIL_CastOpcode typeConvertable(BaseType Vt1, BaseType Vt2);



class BasicBlock;
class Instruction;

/// Base class for AST nodes in the typed intermediate language.
class SExpr {
public:
  TIL_Opcode opcode() const { return static_cast<TIL_Opcode>(Opcode); }

  /// Return true if this is a trivial SExpr (constant or variable name).
  bool isTrivial() const;

  /// Return true if this SExpr is a value (e.g. function, record, constant)
  bool isValue() const;

  /// Return true if this SExpr is a memory-allocated value (e.g. function)
  bool isMemValue() const;

  /// Cast this SExpr to a CFG instruction, or return null if it is not one.
  Instruction* asCFGInstruction();

  const Instruction* asCFGInstruction() const {
    return const_cast<SExpr*>(this)->asCFGInstruction();
  }

  /// Allocate SExpr in the given region.  SExprs must be allocated in regions.
  void *operator new(size_t S, MemRegionRef &R) {
    return ::operator new(S, R);
  }

  /// SExpr objects cannot be deleted.
  // This declaration is public to workaround a gcc bug that breaks building
  // with REQUIRES_EH=1.
  void operator delete(void *) = delete;

  /// Get annotation of the specified derived type. Returns nullptr if no such
  /// annotation exists.
  template <class T>
  T *getAnnotation() const {
    if (Annotations == nullptr)
      return nullptr;
    return Annotations->getAnnotation<T>();
  }

  /// Get all annotations of the specified derived type.
  template <class T>
  std::vector<T*> getAllAnnotations() const {
    if (Annotations == nullptr)
      return std::vector<T*>();
    return Annotations->getAllAnnotations<T>();
  }

  void addAnnotation(Annotation *A);

  Annotation *const annotations() const { return Annotations; }

protected:
  SExpr(TIL_Opcode Op, unsigned char SubOp = 0)
    : Opcode(Op), SubOpcode(SubOp), Flags(0), Annotations(nullptr) {}
  SExpr(const SExpr &E)
    : Opcode(E.Opcode), SubOpcode(E.SubOpcode), Flags(E.Flags),
      Annotations(nullptr) {}

  const unsigned char Opcode;
  unsigned char SubOpcode;
  uint16_t Flags;                 ///< For use by subclasses.

private:
  SExpr() = delete;

  /// SExpr objects must be created in an arena.
  void *operator new(size_t) = delete;

  Annotation *Annotations = nullptr;
};



inline SExpr* maybeRegisterFuture(SExpr** Eptr, SExpr* P);

template<class T>
inline T* maybeRegisterFuture(T** Eptr, T* P);


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
    assert((!Ptr || (Ptr->opcode() != COP_Future)) && "Cannot reset future.");
    Ptr = nullptr;
  }

  void reset(T* P) {
    assert((!Ptr || (Ptr->opcode() != COP_Future)) && "Cannot reset future.");
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


/// PValues (pointer values) are large values that must be allocated on
/// the heap, and referenced via a pointer.  (i.e. reference types).  Examples
/// include functions, records, objects, and boxed values.  Small values
/// that can fit in registers (e.g. bool/int/float) are classified as
/// Instructions instead.
///
/// A PValue expression is a constant expression that declares or defines a
/// value, e.g., a class or function definition.  The Alloc instruction can
/// be used to create a new (possibly mutable) object from a PValue.
class PValue : public SExpr {
public:
  static bool classof(const SExpr *E) {
    return E->opcode() >= COP_Function  &&  E->opcode() <= COP_Record;
  }

  PValue(TIL_Opcode Op, unsigned char SubOp = 0) : SExpr(Op, SubOp) { }
  PValue(const SExpr &E) : SExpr(E) { }
};


/// Instructions are expressions with computational effect that can appear
/// inside basic blocks.
class Instruction : public SExpr {
public:
  static bool classof(const SExpr *E) {
    return E->opcode() >= COP_Literal  &&  E->opcode() <= COP_Undefined;
  }

  static const unsigned InvalidInstrID = 0xFFFFFFFF;

  Instruction(TIL_Opcode Op, unsigned char SubOp = 0)
      : SExpr(Op, SubOp), BType(BaseType::getBaseType<void>()),
        InstrID(0), StackID(0), Block(nullptr) { }
  Instruction(const Instruction &E)
      : SExpr(E), BType(E.BType),
        InstrID(0), StackID(0), Block(nullptr) { }

  /// Return the simple scalar type (e.g. int/float/pointer) of this instr.
  BaseType baseType() const { return BType; }

  /// Returns the instruction ID for this instruction.
  /// A value of 0 means no or unassigned ID.
  /// All basic block instructions have an ID that is unique within the CFG.
  unsigned instrID() const { return InstrID; }

  /// Returns the position of the result of this instruction on the stack.
  /// Can be used when interpreting a program using a stack machine.
  unsigned stackID() const { return StackID; }

  /// Returns the block, if this is an instruction in a basic block,
  /// otherwise returns null.
  BasicBlock* block() const { return Block; }

  /// Return the name (if any) of this instruction.
  StringRef instrName() const;

  /// Set the basic block and instruction ID for this instruction.
  void setInstrID(unsigned id) { InstrID = id; }

  /// Set the basic block for this instruction.
  void setBlock(BasicBlock *B) { Block = B; }

  /// Set the stack ID for this instruction.
  void setStackID(unsigned D) { StackID = D; }

  /// Sets the BaseType for this instruction.
  void setBaseType(BaseType Vt) { BType = Vt; }

  /// Set the name for this instruction.
  void setInstrName(CFGBuilder &Builder, StringRef Name);

protected:
  BaseType      BType;      ///< The scalar type (simple type) of this instr.
  unsigned      InstrID;    ///< An ID that is unique within the CFG.
  unsigned      StackID;    ///< An ID for stack machine interpretation.
  BasicBlock*   Block;      ///< The basic block where this instruction occurs.
};


inline Instruction* SExpr::asCFGInstruction() {
  Instruction* I = dyn_cast<Instruction>(this);
  if (I && I->instrID() > 0)
    return I;
  return nullptr;
}



/// Placeholder for an expression that has not yet been created.
/// Used to implement lazy copy and rewriting strategies.
/// It is classified as an instruction so that it can be used in places
/// where an instruction is required, but it may not produce an instruction
/// when forced.
class Future : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Future; }

  enum FutureStatus : unsigned char {
    FS_pending,     ///< Not yet evaluated.
    FS_evaluating,  ///< Currently being evaluated.
    FS_done         ///< Already evaluated.
  };

  Future() : Instruction(COP_Future), Status(FS_pending),
             Result(nullptr), IPos(nullptr)  { }

  virtual ~Future() { }   // Eliminate virtual destructor warning.

  // We have to undelete this method, because we have a destructor.
  void operator delete(void *Ptr) {
    assert(false && "Cannot delete bump-allocated structures.");
  }

public:
  // Return the result of this future if it exists, otherwise return null.
  SExpr *maybeGetResult() const { return Result; }
  FutureStatus status() const { return Status; }
  void setStatus(FutureStatus FS) { Status = FS; }

  // Connect this future to the given position.
  // Forcing the future will overwrite the value at the position.
  SExpr* addPosition(SExpr **Eptr);

  // Connect this future to the given position in a basic block.
  void addInstrPosition(Instruction **Iptr);

  /// Derived classes must override evaluate to compute the future.
  virtual SExpr* evaluate() = 0;

  /// Return the result, calling evaluate() and setResult() if necessary.
  SExpr* force();

  /// Set the result of this future, and overwrite occurrences with the result.
  void setResult(SExpr *Res);

  SExpr *getResult() const { return Result; }

private:
  FutureStatus Status;
  SExpr *Result;                    ///< Result of forcing this future.
  Instruction** IPos;               ///< Backpointer to CFG loc where F occurs.
  std::vector<SExpr**> Positions;   ///< Backpointers to places where F occurs.
};


inline SExpr* maybeRegisterFuture(SExpr** Eptr, SExpr* P) {
  if (auto *Fut = dyn_cast_or_null<Future>(P))
    return Fut->addPosition(Eptr);
  return P;
}

template<class T>
inline T* maybeRegisterFuture(T** Eptr, T* P) {
  // Futures can only be stored in places that can hold any SExpr.
  assert(!P || P->opcode() != COP_Future);
  return P;
}



/// Simple scalar types, e.g. Int, Float, etc.
class ScalarType : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_ScalarType; }

  ScalarType(BaseType BT) : SExpr(COP_ScalarType), BType(BT)  { }

  /// Return the type of this instruction
  BaseType baseType() const { return BType; }

private:
  BaseType BType;
};



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
  };

  VarDecl(VariableKind K, StringRef s, SExpr *D)
      : SExpr(COP_VarDecl, K), VarIndex(0), VarName(s), Definition(D) { }

  void rewrite(SExpr *D) { Definition.reset(D); }

  /// Return the kind of variable (let, function param, or self)
  VariableKind kind() const { return static_cast<VariableKind>(SubOpcode); }

  /// Return the de-bruin index of the variable.  Counting starts at 1.
  unsigned varIndex() const { return VarIndex; }

  /// Return the name of the variable, if any.
  StringRef varName() const { return VarName; }

  /// Return the definition of the variable.
  /// For let-vars, this is the setting expression.
  /// For function and self parameters, it is the type of the variable.
  SExpr *definition() { return Definition.get(); }
  const SExpr *definition() const { return Definition.get(); }

  void setVarIndex(unsigned i) { VarIndex = i; }
  void setDefinition(SExpr *E) { Definition.reset(E); }

private:
  friend class Function;
  friend class Let;

  unsigned  VarIndex;      // The de-bruin index of the variable.
  StringRef VarName;       // The name of the variable.
  SExprRef  Definition;    // The TIL type or definition.
};


/// A function -- a.k.a. a lambda abstraction.
/// Functions with multiple arguments are created by currying,
/// e.g. (Function (x: Int) (Function (y: Int) (Code { return x + y; })))
class Function : public PValue {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Function; }

  Function(VarDecl *Vd, SExpr *Bd)
      : PValue(COP_Function), VDecl(Vd), Body(Bd) {
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

  void setBody(SExpr* B) { Body.reset(B); }

  bool isSelfApplicable() const { return VDecl->kind() == VarDecl::VK_SFun; }

private:
  SExprRefT<VarDecl> VDecl;
  SExprRef           Body;
};


/// A block of code -- e.g. the body of a function.
class Code : public PValue {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Code; }

  enum CallingConvention : uint16_t {
    CallingConvention_C,
    CallingConvention_CPlusPlus,
    CallingConvention_OhmuInternal
  };

  Code(SExpr *T, SExpr *B) : PValue(COP_Code), ReturnType(T), Body(B) {}

  void rewrite(SExpr *T, SExpr *B) {
    ReturnType.reset(T);
    Body.reset(B);
  }

  CallingConvention callingConvention() {
    return static_cast<CallingConvention>(Flags);
  }
  void setCallingConvention(CallingConvention CCV) {
    Flags = CCV;
  }

  SExpr *returnType() { return ReturnType.get(); }
  const SExpr *returnType() const { return ReturnType.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

  void setBody(SExpr* B) { Body.reset(B); }

private:
  SExprRef ReturnType;
  SExprRef Body;
};


/// A typed, writable location in memory
class Field : public PValue {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Field; }

  Field(SExpr *R, SExpr *B) : PValue(COP_Field), Range(R), Body(B) {}

  void rewrite(SExpr *R, SExpr *B) {
    Range.reset(R);
    Body.reset(B);
  }

  SExpr *range() { return Range.get(); }
  const SExpr *range() const { return Range.get(); }

  SExpr *body() { return Body.get(); }
  const SExpr *body() const { return Body.get(); }

private:
  SExprRef Range;
  SExprRef Body;
};


/// A Slot (i.e. a named definition) in a Record.
class Slot : public PValue {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Slot; }

  enum SlotKind : uint16_t {
    SLT_Normal   = 0,
    SLT_Final    = 1,
    SLT_Override = 2
  };

  Slot(StringRef N, SExpr *D) : PValue(COP_Slot), SlotName(N), Definition(D) { }

  void rewrite(SExpr *D) { Definition.reset(D); }

  StringRef slotName() const { return SlotName; }

  SExpr *definition() { return Definition.get(); }
  const SExpr *definition() const { return Definition.get(); }

  uint16_t modifiers() { return Flags; }
  void     setModifiers(uint16_t M) { Flags = M; }

  bool     hasModifier  (SlotKind K) { return (Flags & K) != 0; }
  void     setModifier  (SlotKind K) { Flags = Flags | K;  }
  void     clearModifier(SlotKind K) { Flags = Flags & ~K; }

private:
  StringRef SlotName;
  SExprRef  Definition;
};


/// A record, which is similar to a C struct.
/// A record is essentially a function from slot names to definitions.
class Record : public PValue {
public:
  typedef ArrayTree<SExprRefT<Slot>> SlotArray;
  typedef DenseMap<std::string, unsigned> SlotMap;

  static bool classof(const SExpr *E) { return E->opcode() == COP_Record; }

  Record(MemRegionRef A, unsigned NSlots, SExpr* P = nullptr)
    : PValue(COP_Record), Parent(P), Slots(A, NSlots), SMap(nullptr) {}

  void rewrite(SExpr *P) { Parent.reset(P); }

  SExpr* parent() { return Parent.get(); }
  const SExpr* parent() const { return Parent.get(); }

  SlotArray& slots() { return Slots; }
  const SlotArray& slots() const { return Slots; }

  void addSlot(MemRegionRef A, Slot *S) { Slots.emplace_back(A, S); }

  Slot* findSlot(StringRef S);

private:
  SExprRef  Parent;   ///< The record we inherit from
  SlotArray Slots;    ///< The slots in the record.
  SlotMap*  SMap;     ///< A map from slot names to indices.
};



template <class T> class LiteralT;

/// Base class for literal values.
class Literal : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Literal; }

  Literal(BaseType BT) : Instruction(COP_Literal) { BType = BT; }
  Literal(const Literal &L) : Instruction(L) { }

  template<class T> const LiteralT<T>* as() const {
    return static_cast<const LiteralT<T>*>(this);
  }
  template<class T> LiteralT<T>* as() {
    return static_cast<LiteralT<T>*>(this);
  }
};


/// Derived class for literal values, which stores the actual value.
template<class T>
class LiteralT : public Literal {
public:
  LiteralT(T Dat) : Literal(BaseType::getBaseType<T>()), Val(Dat) { }

  T  value() const { return Val;}
  T& value() { return Val; }

private:
  T Val;
};


/// A variable, which refers to a previously declared VarDecl.
class Variable : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Variable; }

  Variable(VarDecl *VD) : Instruction(COP_Variable), VDecl(VD) { }

  void rewrite(VarDecl *D) { VDecl.reset(D); }

  const VarDecl* variableDecl() const { return VDecl.get(); }
  VarDecl* variableDecl() { return VDecl.get(); }

  StringRef varName() const { return VDecl->varName(); }

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

  enum ApplyKind : unsigned char {
    FAK_Apply = 0,   // Application of a normal function
    FAK_SApply       // Self-application
  };

  Apply(SExpr *F, SExpr *A, ApplyKind K = FAK_Apply)
      : Instruction(COP_Apply, K), Fun(F), Arg(A)
  {}

  void rewrite(SExpr *F, SExpr *A) {
    Fun.reset(F);
    Arg.reset(A);
  }

  ApplyKind applyKind() const { return static_cast<ApplyKind>(SubOpcode); }

  bool isSelfApplication() const { return SubOpcode == FAK_SApply; }
  bool isDelegation() const { return isSelfApplication() && Arg != nullptr; }

  SExpr *fun() { return Fun.get(); }
  const SExpr *fun() const { return Fun.get(); }

  SExpr *arg() { return Arg.get(); }
  const SExpr *arg() const { return Arg.get(); }

private:
  SExprRef Fun;
  SExprRef Arg;
};


/// Project a named slot from a record.  (Struct or class.)
class Project : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Project; }

  static const short PRJ_Arrow   = 0x01;
  static const short PRJ_Foreign = 0x02;

  Project(SExpr *R, StringRef SName)
      : Instruction(COP_Project), Rec(R), SlotName(SName),
        SlotDecl(nullptr)  { }
  Project(SExpr *R, Slot* Sd)
      : Instruction(COP_Project), Rec(R), SlotName(Sd->slotName()),
        SlotDecl(Sd)  { }

  void rewrite(SExpr *R) { Rec.reset(R); }

  SExpr *record() { return Rec.get(); }
  const SExpr *record() const { return Rec.get(); }

  Slot *slotDecl() { return SlotDecl; }
  const Slot* slotDecl() const { return SlotDecl; }

  // Flag for pretty-printing Ohmu expressions in C++ syntax.
  bool isArrow() const { return (Flags & PRJ_Arrow) != 0; }
  void setArrow(bool b) {
    if (b) Flags |= PRJ_Arrow;
    else Flags &= ~PRJ_Arrow;
  }

  // Flag for projections that refer to foreign (e.g. C++) members.
  bool isForeign() const { return (Flags & PRJ_Foreign) != 0; }
  template<class T>
  const T* getForeignSlotDecl() const {
    assert(isForeign() && "Not a foreign projection.");
    return reinterpret_cast<const T*>(SlotDecl);
  }
  template<class T>
  void setForeignSlotDecl(const T* Ptr) {
    Flags |= PRJ_Foreign;
    SlotDecl = reinterpret_cast<Slot*>(const_cast<T*>(Ptr));
  }

  StringRef slotName() const { return SlotName; }

private:
  SExprRef  Rec;
  StringRef SlotName;
  Slot*     SlotDecl;
};


/// Call a function (after all arguments have been applied).
class Call : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Call; }

  Call(SExpr *T) : Instruction(COP_Call), Target(T) { }

  Code::CallingConvention callingConvention() {
    return static_cast<Code::CallingConvention>(Flags);
  }
  void setCallingConvention(Code::CallingConvention CCV) {
    Flags = CCV;
  }

  void rewrite(SExpr *T) { Target.reset(T); }

  SExpr *target() { return Target.get(); }
  const SExpr *target() const { return Target.get(); }

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

  Alloc(SExpr *E, AllocKind K) : Instruction(COP_Alloc, K), InitExpr(E) {
    setBaseType(BaseType::getBaseType<void*>());
  }

  void rewrite(SExpr *I) { InitExpr.reset(I); }

  AllocKind allocKind() const { return static_cast<AllocKind>(SubOpcode); }

  void setAllocKind(AllocKind K) { SubOpcode = K; }

  bool isLocal() const { return allocKind() == AK_Local; }
  bool isStack() const { return allocKind() == AK_Stack; }
  bool isHeap()  const { return allocKind() == AK_Heap;  }

  SExpr *initializer() { return InitExpr.get(); }
  const SExpr *initializer() const { return InitExpr.get(); }

  // For an alloca, return an index into a virtual stack.
  // Used for SSA renaming and abstract interpretation.
  unsigned allocID() const { return AllocID; }

  void setAllocID(unsigned I) { AllocID = I; }

private:
  SExprRef InitExpr;
  unsigned AllocID;
};


/// Load a value from memory.
class Load : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Load; }

  Load(SExpr *P) : Instruction(COP_Load), Ptr(P) {}

  void rewrite(SExpr *P) { Ptr.reset(P); }

  SExpr *pointer() { return Ptr.get(); }
  const SExpr *pointer() const { return Ptr.get(); }

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

  void rewrite(SExpr *D, SExpr *S) {
    Dest.reset(D);
    Source.reset(S);
  }

  SExpr *destination() { return Dest.get(); }  // Address to store to
  const SExpr *destination() const { return Dest.get(); }

  SExpr *source() { return Source.get(); }     // Value to store
  const SExpr *source() const { return Source.get(); }

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

  void rewrite(SExpr *A, SExpr *I) {
    Array.reset(A);
    Index.reset(I);
  }

  SExpr *array() { return Array.get(); }
  const SExpr *array() const { return Array.get(); }

  SExpr *index() { return Index.get(); }
  const SExpr *index() const { return Index.get(); }

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

  void rewrite(SExpr *A, SExpr *I) {
    Array.reset(A);
    Index.reset(I);
  }

  SExpr *array() { return Array.get(); }
  const SExpr *array() const { return Array.get(); }

  SExpr *index() { return Index.get(); }
  const SExpr *index() const { return Index.get(); }

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

  void rewrite(SExpr *E) { Expr0.reset(E); }

  TIL_UnaryOpcode unaryOpcode() const {
    return static_cast<TIL_UnaryOpcode>(SubOpcode);
  }

  SExpr *expr() { return Expr0.get(); }
  const SExpr *expr() const { return Expr0.get(); }

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

  void rewrite(SExpr *E) { Expr0.reset(E); }

  TIL_CastOpcode castOpcode() const {
    return static_cast<TIL_CastOpcode>(SubOpcode);
  }

  SExpr *expr() { return Expr0.get(); }
  const SExpr *expr() const { return Expr0.get(); }

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
  enum Status : uint16_t {
    PH_MultiVal = 0, // Phi node has multiple distinct values.  (Normal)
    PH_SingleVal,    // Phi node has one distinct value, and can be eliminated
    PH_Incomplete    // Phi node is incomplete
  };

  static bool classof(const SExpr *E) { return E->opcode() == COP_Phi; }

  Phi() : Instruction(COP_Phi) { }
  Phi(MemRegionRef A, unsigned Nvals, Alloc* Lv = nullptr)
      : Instruction(COP_Phi), Values(A, Nvals) { }

  unsigned numValues() const { return Values.size(); }

  /// Return the array of Phi arguments
  const ValArray &values() const { return Values; }
  ValArray &values() { return Values; }

  Status status() const { return static_cast<Status>(Flags); }
  void setStatus(Status s) { Flags = s; }

private:
  ValArray Values;
};


/// Base class for basic block terminators:  Branch, Goto, and Return.
class Terminator : public Instruction {
public:
  static bool classof(const SExpr *E) {
    return E->opcode() >= COP_Goto && E->opcode() <= COP_Return;
  }

  typedef MutArrayRef<SExprRefT<BasicBlock>> BlockArray;

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

  void rewrite(SExpr *R) { Retval.reset(R); }

  /// Return an empty list.
  BlockArray successors() { return BlockArray(); }

  SExpr *returnValue() { return Retval.get(); }
  const SExpr *returnValue() const { return Retval.get(); }

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

  static const unsigned InvalidBlockID = 0x7FFFFFFF;

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
  size_t  blockID() const { return BlockID; }
  void setBlockID(size_t i) { BlockID = i; }

  size_t numArguments()    const { return Args.size(); }
  size_t numInstructions() const { return Instrs.size(); }
  size_t numPredecessors() const { return Predecessors.size(); }
  size_t numSuccessors()   const { return successors().size(); }

  unsigned firstInstrID() {
    if (Args.size() > 0)
      return Args[0]->instrID();
    else if (Instrs.size() > 0)
      return Instrs[0]->instrID();
    return 0;
  }

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
    E->setBlock(this);
    Args.emplace_back(Arena, E);
  }

  /// Add a new instruction.
  void addInstruction(Instruction *E) {
    E->setBlock(this);
    Instrs.emplace_back(Arena, E);
    if (auto *F = dyn_cast<Future>(E))
      F->addInstrPosition(&Instrs.back());
  }

  /// Set the terminator.
  void setTerminator(Terminator *E) {
    TermInstr = E;
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

  explicit BasicBlock(MemRegionRef A)
      : SExpr(COP_BasicBlock), Arena(A), CFGPtr(nullptr), BlockID(0),
        TermInstr(nullptr),
        PostBlockID(0), Depth(0), LoopDepth(0) { }

private:
  friend class SCFG;

  unsigned renumber(unsigned id);   // assign unique ids to all instructions
  int  topologicalSort    (BasicBlock **Blocks, int ID);
  int  postTopologicalSort(BasicBlock **Blocks, int ID);
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
  unsigned numInstructions() const { return NumInstructions; }

  inline void add(BasicBlock *BB) {
    assert(BB->CFGPtr == nullptr);
    BB->CFGPtr = this;
    Blocks.emplace_back(Arena, BB);
  }

  void setEntry(BasicBlock *BB) { Entry = BB; }
  void setExit(BasicBlock *BB)  { Exit = BB;  }

  void renumber();         // assign unique ids to all instructions and blocks
  void computeNormalForm();

  SCFG(MemRegionRef A, unsigned Nblocks)
      : SExpr(COP_SCFG), Arena(A), Blocks(A, Nblocks),
        Entry(nullptr), Exit(nullptr), NumInstructions(0), Normal(false) { }

private:
  MemRegionRef Arena;
  BlockArray   Blocks;
  BasicBlock   *Entry;
  BasicBlock   *Exit;
  unsigned     NumInstructions;
  bool         Normal;
};



/// Placeholder for expressions that cannot be represented in the TIL.
class Undefined : public Instruction {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Undefined; }

  Undefined() : Instruction(COP_Undefined) { }
};


/// Placeholder for a wildcard that matches any other expression.
class Wildcard : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Wildcard; }

  Wildcard() : SExpr(COP_Wildcard) {}
};


/// An identifier, e.g. 'foo' or 'x'.
/// This is a pseduo-term; it will be lowered to a variable or projection.
class Identifier : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Identifier; }

  Identifier(StringRef Id): SExpr(COP_Identifier), IdString(Id) { }

  StringRef idString() const { return IdString; }

private:
  StringRef IdString;
};


/// A let-expression,  e.g.  let x=t; u.
/// This is a pseduo-term; it will be lowered to instructions in a CFG.
class Let : public SExpr {
public:
  static bool classof(const SExpr *E) { return E->opcode() == COP_Let; }

  Let(VarDecl *Vd, SExpr *Bd) : SExpr(COP_Let), VDecl(Vd), Body(Bd) {
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

private:
  SExprRef Condition;
  SExprRef ThenExpr;
  SExprRef ElseExpr;
};


inline bool Goto::isBackEdge() const {
  return TargetBlock->blockID() <= block()->blockID();
}

}  // end namespace til
}  // end namespace ohmu

#endif   // OHMU_TIL_TIL_H
