#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct IR {
    Array<Function> functions;

    Array<const char*> libraries;
};

Result<IR> generate_ir(Array<File> files);