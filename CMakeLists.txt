cmake_minimum_required(VERSION 3.8)

enable_testing()

project(SimpleCompiler LANGUAGES CXX)

add_executable(compiler
    src/array.h
    src/list.h
    src/result.h
    src/platform.h
    src/architecture.h

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

    src/ir.h
    src/ir.cpp

    src/ir_generator.h
    src/ir_generator.cpp

    src/c_generator.h
    src/c_generator.cpp

    src/main.cpp
)

target_compile_features(compiler PRIVATE cxx_std_11)
if(MSVC)
    set_target_properties(compiler PROPERTIES LINK_FLAGS /STACK:10000000)
else()
    set_target_properties(compiler PROPERTIES LINK_FLAGS -Wl,--stack,10000000)
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