
The Ohmu Typed Intermediate Language (TIL) has been carefully designed to be
language-agnostic.  The hope is that many different source languages can be
translated down to the TIL, so that the ohmu static analysis can be made
language agnostic.

The clang C++ compiler is the only language front-end which currently can
translate down to the TIL, and it uses the TIL to do thread safety analysis.
Consequently, the TIL classes are actually implemented in the clang upstream
repository, and are duplicated here.

IMPORTANT:  All files in the directory, with the exception of ThreadSafetyUtil.h,
are copied from clang upstream (see http://clang.llvm.org/).  Do not attempt to
edit these files.  Any changes will be overwritten with the latest versions
from clang upstream.  Clang files retain the original LLVM copyright.
