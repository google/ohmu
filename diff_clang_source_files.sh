#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Til
CLANG_LIB="$1"/tools/clang/lib/Analysis/Til

diff -u "$CLANG_INC"/base/SimpleArray.h  src/base/SimpleArray.h
diff -u "$CLANG_INC"/base/MutArrayRef.h  src/base/MutArrayRef.h
diff -u "$CLANG_INC"/base/ArrayTree.h    src/base/ArrayTree.h

diff -u "$CLANG_INC"/TILOps.def          src/til/TILOps.def
diff -u "$CLANG_INC"/TILBaseType.h       src/til/TILBaseType.h
diff -u "$CLANG_INC"/TIL.h               src/til/TIL.h
diff -u "$CLANG_INC"/TILTraverse.h       src/til/TILTraverse.h
diff -u "$CLANG_INC"/TILCompare.h        src/til/TILCompare.h
diff -u "$CLANG_INC"/TILPrettyPrint.h    src/til/TILPrettyPrint.h
diff -u "$CLANG_INC"/CFGBuilder.h        src/til/CFGBuilder.h
diff -u "$CLANG_INC"/AttributeGrammar.h  src/til/AttributeGrammar.h
diff -u "$CLANG_INC"/CopyReducer.h       src/til/CopyReducer.h

diff -u "$CLANG_LIB"/TIL.cpp             src/til/TIL.cpp
diff -u "$CLANG_LIB"/CFGBuilder.cpp      src/til/CFGBuilder.cpp
