#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Analyses
CLANG_LIB="$1"/tools/clang/lib/Analysis
OHMU_LIB=src/clang/Analysis/Analyses

cp -v "$OHMU_LIB"/ThreadSafetyOps.def    "$CLANG_INC"/
cp -v "$OHMU_LIB"/ThreadSafetyTIL.h      "$CLANG_INC"/
cp -v "$OHMU_LIB"/ThreadSafetyTraverse.h "$CLANG_INC"/
cp -v "$OHMU_LIB"/ThreadSafetyTIL.cpp    "$CLANG_LIB"/
