#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

cp -v ClangCFGWalker.h    "$CLANG_INC"
cp -v ClangTranslator.h   "$CLANG_INC"

cp -v ClangTranslator.cpp "$CLANG_LIB"

