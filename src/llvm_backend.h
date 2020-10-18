#pragma once

#include "ir.h"
#include "result.h"

struct NameMapping {
    RuntimeStatic *runtime_static;

    String name;
};

Result<Array<NameMapping>> generate_llvm_object(
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *object_file_path,
    Array<String> reserved_names
);