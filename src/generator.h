#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct IR {
    Array<RuntimeStatic*> statics;

    Array<const char*> libraries;
};

Result<IR> generate_ir(const char *main_file_path, Array<Statement*> main_file_statements, RegisterSize address_size, RegisterSize default_size);