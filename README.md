# Simple Compiler
A toy compiler handwritten in a weird subset of C++.

The language is a staticly typed, low-level systems programming language very similar to Jonathan Blow's Jai. It is compiled to an internal IR, then to C as a backend (using clang & lld).

The compiler also includes a profiler (for the compiler itself) that outputs trace logs viewable with the https://www.speedscope.app