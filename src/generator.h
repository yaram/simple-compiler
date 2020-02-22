#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct IR {
    Array<Function> functions;

    Array<const char*> libraries;

    Array<StaticConstant> constants;
};

Result<IR> generate_ir(Array<File> files, RegisterSize address_size, RegisterSize default_size);