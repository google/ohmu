
Ohmu is a new programming language being developed at Google.  Right now, it
is just a hobby project that a few engineers are working on in their spare
time.  Its purpose is to serve as a sandbox for experimenting with various
compiler technologies, such as type systems, partial evaluation, run-time
code generation, and GPU programming.

*Disclaimer*:  There are no plans to use ohmu internally or to release it as
a real product.  Anything and everything is subject to change without
notice.  May be known to the State of California to cause cancer in lab
animals, including engineers in computer labs. Do not eat.


Build Instructions:

(1) Install the ohmu source code into a directory, e.g. ohmu.
(2) mkdir ohmu_build   (make this directory alongside ohmu)
(3) cd ohmu_build
(4) ccmake ../ohmu
(5) Go to advanced mode
(6) [Optional] Change compiler to clang.
(7) Change LLVM_DIR to "$LLVM_INSTALL_DIR"/share/llvm/cmake/



