#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

cp -v "$CLANG_INC"/base/SimpleArray.h  src/base/
cp -v "$CLANG_INC"/base/MutArrayRef.h  src/base/
cp -v "$CLANG_INC"/base/ArrayTree.h    src/base/

cp -v "$CLANG_INC"/TILOps.def          src/til/
cp -v "$CLANG_INC"/TILBaseType.h       src/til/
cp -v "$CLANG_INC"/TIL.h               src/til/
cp -v "$CLANG_INC"/TILTraverse.h       src/til/
cp -v "$CLANG_INC"/TILCompare.h        src/til/
cp -v "$CLANG_INC"/TILPrettyPrint.h    src/til/
cp -v "$CLANG_INC"/CFGBuilder.h        src/til/
cp -v "$CLANG_INC"/AttributeGrammar.h  src/til/
cp -v "$CLANG_INC"/CopyReducer.h       src/til/

cp -v "$CLANG_LIB"/TIL.cpp             src/til/
cp -v "$CLANG_LIB"/CFGBuilder.cpp      src/til/

