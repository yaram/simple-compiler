#pragma once

#include "ir.h"
#include "result.h"

bool generate_c_object(
    Array<RuntimeStatic*> statics,
    const char *architecture,
    const char *os,
    const char *config,
    const char *object_file_path
);