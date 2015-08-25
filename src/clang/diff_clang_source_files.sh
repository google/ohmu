#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

diff -u "$CLANG_INC"/ClangCFGWalker.h    ClangCFGWalker.h
diff -u "$CLANG_INC"/ClangTranslator.h   ClangTranslator.h

diff -u "$CLANG_LIB"/ClangTranslator.cpp ClangTranslator.cpp

