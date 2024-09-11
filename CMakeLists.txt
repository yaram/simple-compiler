cmake_minimum_required(VERSION 3.21)

enable_testing()

project(SimpleCompiler LANGUAGES CXX)

# Ensure below options are respected by llvm CMake config
set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)

set(LLVM_INCLUDE_TOOLS OFF)
set(LLVM_INCLUDE_TESTS OFF)
set(LLVM_INCLUDE_EXAMPLES OFF)
set(LLVM_INCLUDE_DOCS OFF)
set(LLVM_INCLUDE_BENCHMARKS OFF)
set(LLVM_INCLUDE_RUNTIMES OFF)
set(LLVM_INCLUDE_UTILS OFF)
set(LLVM_TARGETS_TO_BUILD X86;WebAssembly)

add_subdirectory(thirdparty/llvm-project/llvm)

# Because CMake won't actually tell me where the output directory is in any variables or target properties, they're all blank, I hate this
if(CMAKE_CONFIGURATION_TYPES)
    set(ACTUAL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    set(ACTUAL_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

add_custom_target(
    copy_runtimes
    COMMAND cmake -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/src/runtimes" "${ACTUAL_OUTPUT_DIRECTORY}"
)

set(sources
    src/array.h
    src/list.h
    src/result.h
    src/jobs.h

    src/profiler.h

    src/string.h
    src/string.cpp

    src/platform.h
    src/platform.cpp

    src/path.h
    src/path.cpp

    src/timing.h
    src/timing.cpp

    src/util.h
    src/util.cpp

    src/tokens.h
    src/tokens.cpp

    src/lexer.h
    src/lexer.cpp

    src/calling_convention.h
    src/calling_convention.cpp

    src/ast.h
    src/ast.cpp

    src/parser.h
    src/parser.cpp

    src/hlir.h
    src/hlir.cpp

    src/register_size.h
    src/register_size.cpp

    src/types.h
    src/types.cpp

    src/constant.h
    src/constant.cpp

    src/hl_generator.h
    src/hl_generator.cpp

    src/hl_llvm_backend.h
    src/hl_llvm_backend.cpp

    src/main.cpp
)

if(PROFILING)
    set(sources ${sources} src/profiler.cpp)
endif()

add_executable(compiler ${sources})

target_compile_features(compiler PRIVATE cxx_std_20)
if(PROFILING)
    target_compile_definitions(compiler PRIVATE PROFILING)
endif()
target_link_libraries(compiler PRIVATE LLVMCore LLVMAnalysis LLVMX86CodeGen LLVMWebAssemblyCodeGen)
target_include_directories(compiler PRIVATE thirdparty/llvm-project/llvm/include "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/llvm-project/llvm/include")
add_dependencies(compiler copy_runtimes)

add_executable(test_driver
    src/test_driver.cpp
)
add_dependencies(test_driver compiler)

target_compile_features(test_driver PRIVATE cxx_std_20)

function(single_file_test TEST_NAME)
    add_test(NAME ${TEST_NAME}
        COMMAND test_driver $<TARGET_FILE:compiler> ${CMAKE_CURRENT_SOURCE_DIR}/tests/${TEST_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

function(multi_file_test TEST_NAME)
    add_test(NAME ${TEST_NAME}
        COMMAND test_driver $<TARGET_FILE:compiler> ${CMAKE_CURRENT_SOURCE_DIR}/tests/${TEST_NAME}/main
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

single_file_test(main_return)

single_file_test(function_call)
single_file_test(function_parameters)

multi_file_test(imports)

single_file_test(if_statements)

single_file_test(named_constants)

single_file_test(out_of_order)

single_file_test(constant_integer_arithmetic)
single_file_test(constant_equality_test)
single_file_test(constant_boolean_operators)
single_file_test(constant_bitwise_operators)
single_file_test(constant_bitshift_operators)
single_file_test(constant_integer_casts)
single_file_test(constant_pointer_casts)

single_file_test(variables)

single_file_test(integer_arithmetic)
single_file_test(equality_test)
single_file_test(boolean_operators)
single_file_test(bitwise_operators)
single_file_test(bitshift_operators)
single_file_test(integer_casts)
single_file_test(pointer_casts)

single_file_test(type_elision)

single_file_test(while_statements)

single_file_test(polymorphic_functions)

single_file_test(static_arrays)
single_file_test(constant_arrays)

single_file_test(structs)
single_file_test(unions)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    single_file_test(extern_libs_win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    single_file_test(extern_libs_linux)
endif()