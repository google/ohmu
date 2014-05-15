#!/bin/bash

CLANG_INC="$1"/tools/clang/include/clang/Analysis/Analyses
CLANG_LIB="$1"/tools/clang/lib/Analysis
OHMU_LIB=src/clang/Analysis/Analyses

cp -v "$CLANG_INC"/ThreadSafetyOps.def    "$OHMU_LIB"/
cp -v "$CLANG_INC"/ThreadSafetyTIL.h      "$OHMU_LIB"/
cp -v "$CLANG_INC"/ThreadSafetyTraverse.h "$OHMU_LIB"/
cp -v "$CLANG_LIB"/ThreadSafetyTIL.cpp    "$OHMU_LIB"/
