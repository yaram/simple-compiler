#pragma once

#include "ir.h"
#include "result.h"

Result<const char *> generate_c_source(Array<Function> functions, Array<StaticConstant> constants, ArchitectureInfo architecture_info);