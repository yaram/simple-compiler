# Simple Compiler
A toy compiler handwritten in a weird subset of C++.

The language is a staticly typed, low-level systems programming language very similar to Jonathan Blow's Jai. It is compiled to an internal IR, then LLVM is used to generate native machine code. 

The compiler currently requires clang for compiling the runtime/bootstrap code and lld for linking.

The compiler also includes a profiler (for the compiler itself) that can be enabled with the `-DPROFILING=Yes` option given to CMake.
The profiler trace logs are viewable with [speedscope](https://www.speedscope.app). The profiler currently only works on Windows and x86.

## Building
1. Install LLVM 11 or higher (including clang and lld)
1. Install CMake
1. __(Windows)__ Install the Windows SDK (Only tested on Windows 10)
1. Clone repository
    ```bash
    git clone https://github.com/yaram/simple-compiler.git
    cd simple-compiler
    ```
1. Run CMake configuration
    ```bash
    mkdir build
    cd build

    cmake ..
    ```
1. Run build
    ```bash
    cmake --build . --target compiler
    ```