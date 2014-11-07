
The Ohmu Typed Intermediate Language (TIL) has been carefully designed to be
language-agnostic.  The hope is that many different source languages can be
translated down to the TIL, so that the ohmu static analysis can be made
language agnostic.

The clang C++ compiler uses the TIL to do thread safety analysis.  Many of the
files in this directory are thus mirrored in the clang open source repository,
and can be found under clang/lib/Analysis/Analyses/ThreadSafety/.  Files which
shared with clang are released under the LLVM open source license, rather than
the Apache open source license, and are written in the clang/LLVM style.

