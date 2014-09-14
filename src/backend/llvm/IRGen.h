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


class IRGen : public SimpleReducerBase {
public:
  IRGen() :  currentFunction_(nullptr), builder_(ctx()) {
    // The module holds all of the llvm output.
    outModule_ = new llvm::Module("ohmu_output", ctx());
  }

  ~IRGen() {
     delete outModule_;
  }

  static llvm::LLVMContext& ctx() { return llvm::getGlobalContext(); }

  llvm::Module* module() { return outModule_; }


  llvm::Value* generate(SExpr* e) {
    llvm::Value* v;
    if (e->block()) {
      v = currentValues_[e->id()];
      if (v)
        return v;
    }

    switch (e->opcode()) {
#define TIL_OPCODE_DEF(X)                                                   \
    case COP_##X:                                                           \
      v = generate##X(cast<X>(e)); break;
#include "clang/Analysis/Analyses/ThreadSafetyOps.def"
#undef TIL_OPCODE_DEF
    }

    if (e->block()) {
      currentValues_[e->id()] = v;
    }
    return v;
  }

  llvm::Value* generateFuture(Future* e) {
    return generate(e->result());
  }

  llvm::Value* generateUndefined(Undefined* e)   { return nullptr; }
  llvm::Value* generateWildcard (Wildcard* e)    { return nullptr; }


  // We define part of the reducer interface for literals here.
  typedef llvm::Value* R_SExpr;

  llvm::Value* reduceLiteral(Literal& e) { return nullptr; }

  template <class T>
  llvm::Value* reduceLiteralT(LiteralT<T>& e) { return nullptr; }

  llvm::Value* reduceLiteralT(LiteralT<int32_t>& e) {
    llvm::Constant* c =
      llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(ctx()), e.value());
    // return builder_.Insert(c);
    return c;
  }

  llvm::Value* generateLiteral(Literal* e) {
    return e->traverse(*this, TRV_Normal);
  }

  llvm::Value* generateLiteralPtr(LiteralPtr* e) {
    return nullptr;
  }

  llvm::Value* generateVarDecl(VarDecl* e) {
    if (e->kind() == VarDecl::VK_Let)
      return generate(e->definition());
    return nullptr;
  }

  llvm::Value* generateFunction  (Function* e)   { return nullptr; }
  llvm::Value* generateSFunction (SFunction* e)  { return nullptr; }
  llvm::Value* generateCode      (Code* e)       { return nullptr; }
  llvm::Value* generateField     (Field* e)      { return nullptr; }

  llvm::Value* generateApply     (Apply* e)      { return nullptr; }
  llvm::Value* generateSApply    (SApply* e)     { return nullptr; }
  llvm::Value* generateProject   (Project* e)    { return nullptr; }

  llvm::Value* generateCall      (Call* e)       { return nullptr; }
  llvm::Value* generateAlloc     (Alloc* e)      { return nullptr; }
  llvm::Value* generateLoad      (Load* e)       { return nullptr; }
  llvm::Value* generateStore     (Store* e)      { return nullptr; }
  llvm::Value* generateArrayIndex(ArrayIndex* e) { return nullptr; }
  llvm::Value* generateArrayAdd  (ArrayAdd* e)   { return nullptr; }

  llvm::Value* generateUnaryOp(UnaryOp* e) {
    auto *e0 = generate(e->expr());
    if (!e0)
      return nullptr;

    switch (e->unaryOpcode()) {
      case UOP_Minus:
        return builder_.CreateNeg(e0);
      case UOP_BitNot:
        return builder_.CreateNot(e0);
      case UOP_LogicNot:
        return builder_.CreateNot(e0);
    }
    return nullptr;
  }

  llvm::Value* generateBinaryOp(BinaryOp* e) {
    auto* e0 = generate(e->expr0());
    auto* e1 = generate(e->expr1());
    if (!e0 || !e1)
      return nullptr;

    switch (e->binaryOpcode()) {
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

  llvm::Value* generateCast(Cast* e) {
    return nullptr;
  }

  llvm::Value* generateSCFG(SCFG* e) {
    currentBlocks_.clear();
    currentBlocks_.resize(e->numBlocks(), nullptr);
    currentValues_.clear();
    currentValues_.resize(e->numInstructions(), nullptr);

    // Create a new function in outModule_
    std::vector<llvm::Type*> argTypes;
    llvm::FunctionType *ft =
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx()), argTypes, false);
    currentFunction_ =
      llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ohmu_main",
                             outModule_);

    for (auto* b : *e) {
      generateBasicBlock(b);
    }

    llvm::verifyFunction(*currentFunction_);
    currentFunction_->dump();

    return nullptr;
  }

  llvm::Value* generateBasicBlock(BasicBlock* e) {
    auto* lbb = getBasicBlock(e);
    builder_.SetInsertPoint(lbb);
    for (auto* a : e->arguments()) {
      generate(a);
    }
    for (auto* inst : e->instructions()) {
      generate(inst);
    }
    generate(e->terminator());
    return nullptr;
  }

  llvm::Value* generatePhi(Phi* e) {
    llvm::Type* ty = llvm::Type::getInt32Ty(ctx());
    llvm::PHINode* lph = builder_.CreatePHI(ty, e->values().size());
    BasicBlock* bb = e->block();
    for (unsigned i = 0; i < bb->numPredecessors(); ++i) {
      auto* larg = generate(e->values()[i]);
      auto* lbb = getBasicBlock(bb->predecessors()[i]);
      lph->addIncoming(larg, lbb);
    }
    return lph;
  }

  llvm::Value* generateGoto(Goto* e) {
    auto* lbb = getBasicBlock(e->targetBlock());
    builder_.CreateBr(lbb);
    return nullptr;
  }

  llvm::Value* generateBranch(Branch* e) {
    return nullptr;
  }

  llvm::Value* generateReturn(Return* e) {
    auto* rv = generate(e->returnValue());
    if (!rv)
      return nullptr;
    return builder_.CreateRet(rv);
  }

  llvm::Value* generateIdentifier(Identifier* e) { return nullptr; }
  llvm::Value* generateIfThenElse(IfThenElse* e) { return nullptr; }
  llvm::Value* generateLet       (Let* e)        { return nullptr; }


  inline llvm::BasicBlock* getBasicBlock(BasicBlock* b) {
    llvm::BasicBlock *lbb = currentBlocks_[b->blockID()];
    if (!lbb) {
      if (!currentFunction_)
        return nullptr;
      lbb = llvm::BasicBlock::Create(ctx(), "", currentFunction_);
      currentBlocks_[b->blockID()] = lbb;
    }
    return lbb;
  }


private:
  llvm::Module*     outModule_;
  llvm::Function*   currentFunction_;
  llvm::IRBuilder<> builder_;

  std::vector<llvm::BasicBlock*> currentBlocks_;
  std::vector<llvm::Value*> currentValues_;
};


}  // end namespace backend_llvm
}  // end namespace ohmu


#endif  // OHMU_BACKEND_LLVM_IRGEN_H
