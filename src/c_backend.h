#pragma once

#include "ir.h"
#include "result.h"

struct NameMapping {
    RuntimeStatic *runtime_static;

    const char *name;
};

Result<Array<NameMapping>> generate_c_object(
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *object_file_path
);