#pragma once

#include "ir.h"

struct NameMapping {
    RuntimeStatic *runtime_static;

    const char *name;
};

Result<Array<NameMapping>> generate_llvm_object(
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *object_file_path
);