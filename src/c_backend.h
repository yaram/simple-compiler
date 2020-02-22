#pragma once

#include "ir.h"
#include "result.h"

bool generate_c_object(
    Array<Function> functions,
    Array<StaticConstant> constants,
    const char *architecture,
    const char *os,
    const char *output_directory,
    const char *output_name
);