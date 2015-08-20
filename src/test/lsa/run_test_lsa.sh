#!/bin/bash
#
# Assisting script to call a clang-tool binary with the right arguments to the
# Clang include headers. Requires that the environment variable LLVM_BUILD is
# set to the LLVM build directory that includes Clang.
#
# Call this script as:
#   ./run_test_lsa.sh <path-to-binary> <binary-arguments>
#
BPATH=${LLVM_BUILD:?"Specify the absolute path to the LLVM build directory as the environment variable LLVM_BUILD."}
VERSION=`ls $BPATH/lib/clang | sort -n | tail -1`
COMMAND=$1
shift 1
"$COMMAND" \
  -extra-arg=-I \
  -extra-arg=$BPATH/lib/clang/$VERSION/include \
  $@

