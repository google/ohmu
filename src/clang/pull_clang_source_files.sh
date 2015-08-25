#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

cp -v "$CLANG_INC"/ClangCFGWalker.h    .
cp -v "$CLANG_INC"/ClangTranslator.h   .

cp -v "$CLANG_LIB"/ClangTranslator.cpp .

