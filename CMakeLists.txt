cmake_minimum_required(VERSION 3.1)

project(SimpleCompiler LANGUAGES C)

add_executable(compiler
    src/main.c
)

target_compile_features(compiler PRIVATE c_std_11)