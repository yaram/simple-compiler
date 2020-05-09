cmake_minimum_required(VERSION 3.8)

enable_testing()

project(SimpleCompiler LANGUAGES CXX)

set(sources
    src/array.h
    src/list.h
    src/result.h
    src/jobs.h

    src/profiler.h

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

    src/ast.h
    src/ast.cpp

    src/parser.h
    src/parser.cpp

    src/ir.h
    src/ir.cpp

    src/register_size.h
    src/register_size.cpp

    src/constant.h
    src/constant.cpp

    src/c_backend.h
    src/c_backend.cpp

    src/main.cpp
)

if(PROFILING)
    set(sources ${sources} src/profiler.cpp)
endif()

add_executable(compiler ${sources})

target_compile_features(compiler PRIVATE cxx_std_11)
if(MSVC)
    set_target_properties(compiler PROPERTIES LINK_FLAGS /STACK:10000000)
else()
    set_target_properties(compiler PROPERTIES LINK_FLAGS -Wl,--stack,10000000)
endif()
if(PROFILING)
    target_compile_definitions(compiler PRIVATE PROFILING)
endif()

add_executable(test_driver
    src/test_driver.cpp
)

target_compile_features(test_driver PRIVATE cxx_std_11)

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

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    single_file_test(extern_libs_win32)
endif()

single_file_test(named_constants)

single_file_test(out_of_order)

single_file_test(constant_integer_arithmetic)
single_file_test(constant_equality_test)
single_file_test(constant_boolean_operators)
single_file_test(constant_bitwise_operators)
single_file_test(constant_integer_casts)
single_file_test(constant_pointer_casts)

single_file_test(variables)

single_file_test(integer_arithmetic)
single_file_test(equality_test)
single_file_test(boolean_operators)
single_file_test(bitwise_operators)
single_file_test(integer_casts)
single_file_test(pointer_casts)

single_file_test(type_elision)

single_file_test(while_statements)

single_file_test(polymorphic_functions)

single_file_test(static_arrays)