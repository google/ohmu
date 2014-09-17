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


#include "llvm/IR/Verifier.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

namespace ohmu {
namespace backend_llvm {

using namespace clang::threadSafety::til;

void generate_LLVM_IR(SExpr* E);

}  // end namespace backend_llvm
}  // end namespace ohmu

#endif  // OHMU_BACKEND_LLVM_IRGEN_H
