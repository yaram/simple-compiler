#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct GeneratorResult {
    Array<RuntimeStatic*> statics;

    Array<const char*> libraries;

    uint64_t generator_time;
    uint64_t parser_time;
};

Result<GeneratorResult> generate_ir(const char *main_file_path, Array<Statement*> main_file_statements, RegisterSize address_size, RegisterSize default_size);