.. ohmu documentation master file, created by
   sphinx-quickstart on Sun May 11 10:04:06 2014.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Contents
==================

.. toctree::
   :maxdepth: 2


Introduction
============

Ohmu is a new programming language being developed at Google.  Right now, it
is just a hobby project that a few engineers are working on in their spare
time.  Its purpose is to serve as a sandbox for experimenting with various
compiler technologies, such as type systems, partial evaluation, run-time code
generation, and GPU programming.

*Disclaimer*:  There are no plans to use ohmu internally or to release it as a
real product.  Anything and everything is subject to change without notice.
May be known to the State of California to cause cancer in lab animals,
including engineers in computer labs. Do not eat.

Why a new language?
-------------------

We believe that a good programming language is one where the tool chain
provides as much assistance as possible.  Optimizing compilers improve
performance, type systems and warnings help find bugs, while IDEs and
refactoring tools help organize and maintain code.  These tools are effective
only if the language itself has a well-defined semantics, so that the compiler
or IDE can analyze and understand the code.

Unfortunately, most practical programming languages have a semantics that is
informal, ad-hoc, overly complex, or unsound in various ways, which tends to
confound any attempt at analysis. A great deal of academic research has been
done in the area of formal programming language semantics, but academic
languages go too far in the other direction; they are either too formal (e.g.
Agda) or impractical for most real-world tasks (e.g. Haskell).

Our goal is to collect the best ideas from academic research, combine those
ideas together, and apply them to the design of a modern, elegant, and above
all, a *practical* language.

Overview of Language Features
-----------------------------

* High performance:

  * Faster than C.
  * Suitable for systems programming, games, or scientific computation.
  * Transparent foreign-function interface to C and C++ code.
  * Transparent support for GPGPU programming.
  * Advanced optimizations driven by static analysis (e.g. alias analysis).

* Safe:

  * Type and memory safe.  No unsafe casts or buffer overflows...
  * Thread-safe by design.  No race conditions...
  * Designed from the ground-up for static analysis.

* Modular and high-level:

  * Object-oriented programming: classes, inheritance, generics, and mixins.
  * Functional programming: type-classes, variant data types, and ADTs.
  * Mixin-modules: virtual classes and extensible data types.

* Extensible:

  * *Extensible syntax*: libraries can extend the language with new syntax.
  * *Partial evaluation*: compile language extensions down to the core language.
  * Compile-time reflection and meta-programming.
  * Support for embedding domain-specific languages (DSLs):

    * E.g. parser generators, matrix libraries, image filters, shaders, etc.


Key Technologies
^^^^^^^^^^^^^^^^

Although this may look like a long wish-list, all of these features depend
primarily on just two key technologies:

#. A sophisticated static type system.
#. Partial evaluation.

**Type system**.  Type systems are routinely used to handle structural types,
such as ``int`` and ``String``.  However, they can do much more than that.
Type systems a general purpose tool for declaring and enforcing any program
invariant, including aliasing constraints, ownership, or freedom from race
conditions.  The ohmu type system is responsible for the *safety* features
above, and it is a key part of *modularity*, since mixin modules have very
complex types.  Moreover, the type system enforces program invariants that are
then used by the optimizer to achieve *high performance*.

**Partial evaluation**.  Partial evaluation optimizes code by shifting
computations from run-time to compile-time.  Most compilers can perform
constant propogation.  However, the ohmu compiler can perform arbitrary
computations at compile-time.  Moreover, the partial evaluator is linked to
the static type system, which enables it to perform type-based
transformations, like method de-virtualization.  The *extensibility* features
use partial evaluation to eliminate the run-time overhead that is generally
associated with reflection and DSLs.


Tradeoffs in Language Design
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There has traditionally been a tradeoff in language design between performance
and safety.  A safe language must perform additional run-time checks, such as
array bounds checks, to prevent unsafe operations from occuring. Ohmu
leverages the type system to achieve both performance and safety at the same
time.  E.g. if the type system can prove that the array bounds check always
succeeds, then run-time check can be omitted.

Similarly, there has traditionally been a tradeoff between performance and
high-level abstractions.  Abstractions introduce an additional level of
indirection, which usually has a run-time cost.  For example, object-oriented
inheritance and virtual methods are a useful abstraction, but most compilers
cannot inline virtual methods, so they have a high cost.  Ohmu leverages
partial evaluation to eliminate the overhead of most abstractions.  For
example, the partial evaluator can specialize a polymorphic function to a
concrete type, thus allowing the virtual calls to be eliminated.


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

