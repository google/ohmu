//===- IRGen.h -------------------------------------------------*- C++ --*-===//
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
//
// Defines the LLVM IR Generation layer.
//
//===----------------------------------------------------------------------===//

#ifndef OHMU_BACKEND_LLVM_IRGEN_H
#define OHMU_BACKEND_LLVM_IRGEN_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"

#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

namespace ohmu {
namespace backend_llvm {

using namespace clang::threadSafety::til;


// Map ohmu IR types to LLVM Types.
template <class T> struct LLVMTypeMap { typedef llvm::Value* Ty; };

// These kinds of SExpr must map to the same kind.
// We define these here b/c template specializations cannot be class members.
template<> struct LLVMTypeMap<Phi>        { typedef llvm::PHINode* Ty; };
template<> struct LLVMTypeMap<BasicBlock> { typedef llvm::BasicBlock* Ty; };
template<> struct LLVMTypeMap<SCFG>       { typedef llvm::Function* Ty; };


/// The LLVMReducer maps SExpr* to Value*
class LLVMReducerMap {
public:
  template <class T> struct TypeMap : public LLVMTypeMap<T> { };
};


class LLVMReducer
    : public LLVMReducerMap,
      public UnscopedReducer<LLVMReducerMap>,
      public DefaultReducer<LLVMReducer, LLVMReducerMap> {
public:
  LLVMReducer() :  currentFunction_(nullptr), builder_(ctx()) {
    // The module holds all of the llvm output.
    outModule_ = new llvm::Module("ohmu_output", ctx());
  }

  ~LLVMReducer() {
     delete outModule_;
  }

  static llvm::LLVMContext& ctx() { return llvm::getGlobalContext(); }

  llvm::Module* module() { return outModule_; }

public:
  typedef DefaultContext<LLVMReducer> ContextT;

  // default result of all undefined reduce methods.
  std::nullptr_t reduceNull() { return nullptr; }


  void processResult(SExpr& orig, llvm::Value* v) {
    if (orig.block()) {
      currentValues_[orig.id()] = v;
    }
  }

  llvm::Value* reduceWeakInstr(SExpr* e) {
    return currentValues_[e->id()];
  }

  llvm::BasicBlock* reduceWeakBasicBlock(BasicBlock* b) {
    auto *lbb = currentBlocks_[b->blockID()];
    if (!lbb) {
      if (!currentFunction_)
        return nullptr;
      lbb = llvm::BasicBlock::Create(ctx(), "", currentFunction_);
      currentBlocks_[b->blockID()] = lbb;
    }
    return lbb;
  }


  llvm::Value* reduceLiteral(Literal& e) { return nullptr; }

  template <class T>
  llvm::Value* reduceLiteralT(LiteralT<T>& e) { return nullptr; }

  llvm::Value* reduceLiteralT(LiteralT<int32_t>& e) {
    llvm::Constant* c =
      llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx()), e.value());
    // return builder_.Insert(c);
    return c;
  }

  llvm::Value* reduceUnaryOp(UnaryOp& orig, llvm::Value* e0) {
    if (!e0)
      return nullptr;

    switch (orig.unaryOpcode()) {
      case UOP_Minus:
        return builder_.CreateNeg(e0);
      case UOP_BitNot:
        return builder_.CreateNot(e0);
      case UOP_LogicNot:
        return builder_.CreateNot(e0);
    }
    return nullptr;
  }

  llvm::Value* reduceBinaryOp(BinaryOp& orig, llvm::Value* e0, llvm::Value* e1) {
    if (!e0 || !e1)
      return nullptr;

    switch (orig.binaryOpcode()) {
      case BOP_Add:
        return builder_.CreateAdd(e0, e1);
      case BOP_Sub:
        return builder_.CreateSub(e0, e1);
      case BOP_Mul:
        return builder_.CreateMul(e0, e1);
      case BOP_Div:
        return builder_.CreateSDiv(e0, e1);
      case BOP_Rem:
        return builder_.CreateSRem(e0, e1);
      case BOP_Shl:
        return builder_.CreateShl(e0, e1);
      case BOP_Shr:
        return builder_.CreateLShr(e0, e1);
      case BOP_BitAnd:
        return builder_.CreateAnd(e0, e1);
      case BOP_BitXor:
        return builder_.CreateXor(e0, e1);
      case BOP_BitOr:
        return builder_.CreateOr(e0, e1);
      case BOP_Eq:
        return builder_.CreateICmpEQ(e0, e1);
      case BOP_Neq:
        return builder_.CreateICmpNE(e0, e1);
      case BOP_Lt:
        return builder_.CreateICmpSLT(e0, e1);
      case BOP_Leq:
        return builder_.CreateICmpSLE(e0, e1);
      case BOP_LogicAnd:
        return builder_.CreateAnd(e0, e1);
      case BOP_LogicOr:
        return builder_.CreateOr(e0, e1);
    }
    return nullptr;
  }

  llvm::PHINode* reducePhiBegin(Phi& orig) {
    llvm::Type* ty = llvm::Type::getInt32Ty(ctx());
    llvm::PHINode* lph = builder_.CreatePHI(ty, orig.values().size());
    return lph;
  }

  void reducePhiArg(Phi& orig, llvm::PHINode* lph, unsigned i, llvm::Value *v) {
    BasicBlock* bb = orig.block();
    auto* lbb = reduceWeakBasicBlock(bb->predecessors()[i]);
    lph->addIncoming(v, lbb);
  }

  llvm::Value* reduceGoto(Goto& orig, llvm::BasicBlock* target) {
    auto* lbb = reduceWeakBasicBlock(orig.targetBlock());
    builder_.CreateBr(lbb);
    return nullptr;
  }

  llvm::Value* reduceBranch(Branch& orig) {
    return nullptr;
  }

  llvm::Value* reduceReturn(Return& orig, llvm::Value *rv) {
    if (!rv)
      return nullptr;
    return builder_.CreateRet(rv);
  }

  llvm::BasicBlock* reduceBasicBlockBegin(BasicBlock &orig) {
    auto* lbb = reduceWeakBasicBlock(&orig);
    builder_.SetInsertPoint(lbb);
    return lbb;
  }

  llvm::Function* reduceCFGBegin(SCFG& orig) {
    currentBlocks_.clear();
    currentBlocks_.resize(orig.numBlocks(), nullptr);
    currentValues_.clear();
    currentValues_.resize(orig.numInstructions(), nullptr);

    // Create a new function in outModule_
    std::vector<llvm::Type*> argTypes;
    llvm::FunctionType *ft =
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx()), argTypes, false);
    currentFunction_ =
      llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ohmu_main",
                             outModule_);

    // The current CFG is implicit.
    return currentFunction_;
  }

  llvm::Function* reduceSCFG(llvm::Function* cfg) {
    llvm::verifyFunction(*currentFunction_);
    currentFunction_->dump();
    return cfg;
  }


private:
  llvm::Module*     outModule_;
  llvm::Function*   currentFunction_;
  llvm::IRBuilder<> builder_;

  std::vector<llvm::BasicBlock*> currentBlocks_;
  std::vector<llvm::Value*> currentValues_;
};


class IRGen : public Traversal<IRGen, LLVMReducer> {
public:
  static void generate(SExpr* E) {
    IRGen Traverser;
    LLVMReducer Reducer;
    return Traverser.traverse(E, &Reducer);
  }
};



}  // end namespace backend_llvm
}  // end namespace ohmu


#endif  // OHMU_BACKEND_LLVM_IRGEN_H
