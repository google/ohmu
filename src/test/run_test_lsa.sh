#!/bin/bash
BPATH=${LLVM_BUILD:?"Specify the absolute path to the LLVM build directory as the environment variable LLVM_BUILD."}
VERSION=`ls $BPATH/lib/clang | sort -n | tail -1`
./src/test/test_lsa \
  -extra-arg=-I \
  -extra-arg=$BPATH/lib/clang/$VERSION/include \
  $@

