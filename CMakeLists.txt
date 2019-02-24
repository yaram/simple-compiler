cmake_minimum_required(VERSION 3.1)

project(SimpleCompiler LANGUAGES CXX)

add_executable(compiler
    src/list.h

    src/ast.h
    src/ast.cpp

    src/parser.h
    src/parser.cpp

    src/main.cpp
)

target_compile_features(compiler PRIVATE cxx_std_11)