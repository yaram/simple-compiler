#pragma once

#include "hlir.h"
#include "result.h"

struct NameMapping {
    RuntimeStatic* runtime_static;

    String name;
};

Result<Array<NameMapping>> generate_llvm_object(
    String top_level_source_file_path,
    Array<RuntimeStatic*> statics,
    String architecture,
    String os,
    String toolchain,
    String config,
    String object_file_path,
    Array<String> reserved_names,
    bool print
);