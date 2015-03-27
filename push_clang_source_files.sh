#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

cp -v src/base/SimpleArray.h      "$CLANG_INC"/base/
cp -v src/base/MutArrayRef.h      "$CLANG_INC"/base/
cp -v src/base/ArrayTree.h        "$CLANG_INC"/base/

cp -v src/til/TILOps.def          "$CLANG_INC"
cp -v src/til/TILBaseType.h       "$CLANG_INC"
cp -v src/til/TIL.h               "$CLANG_INC"
cp -v src/til/TILTraverse.h       "$CLANG_INC"
cp -v src/til/TILCompare.h        "$CLANG_INC"
cp -v src/til/TILPrettyPrint.h    "$CLANG_INC"
cp -v src/til/CFGBuilder.h        "$CLANG_INC"
cp -v src/til/AttributeGrammar.h  "$CLANG_INC"
cp -v src/til/CopyReducer.h       "$CLANG_INC"

cp -v src/til/TIL.cpp             "$CLANG_LIB"
cp -v src/til/CFGBuilder.cpp      "$CLANG_LIB"
