#pragma once

#include "ir.h"
#include "result.h"

bool generate_c_object(
    Array<Function> functions,
    Array<StaticConstant> constants,
    ArchitectureInfo architecture_info,
    const char *output_directory,
    const char *output_name
);