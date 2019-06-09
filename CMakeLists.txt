cmake_minimum_required(VERSION 3.1)

project(SimpleCompiler LANGUAGES CXX)

add_executable(compiler
    src/array.h
    src/list.h
    src/result.h
    src/platform.h

    src/path.h
    src/path.cpp

    src/util.h
    src/util.cpp

    src/ast.h
    src/ast.cpp

    src/parser.h
    src/parser.cpp

    src/types.h
    src/types.cpp

    src/generator.h
    src/generator.cpp

    src/main.cpp
)

target_compile_features(compiler PRIVATE cxx_std_11)