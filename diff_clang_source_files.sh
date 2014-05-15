#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Analyses
CLANG_LIB="$1"/tools/clang/lib/Analysis
OHMU_LIB=src/clang/Analysis/Analyses

diff -u "$CLANG_INC"/ThreadSafetyOps.def    "$OHMU_LIB"/ThreadSafetyOps.def
diff -u "$CLANG_INC"/ThreadSafetyTIL.h      "$OHMU_LIB"/ThreadSafetyTIL.h
diff -u "$CLANG_INC"/ThreadSafetyTraverse.h "$OHMU_LIB"/ThreadSafetyTraverse.h
diff -u "$CLANG_LIB"/ThreadSafetyTIL.cpp    "$OHMU_LIB"/ThreadSafetyTIL.cpp
