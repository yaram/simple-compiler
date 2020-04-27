#pragma once

#include <time.h>
#include "ast.h"
#include "ir.h"
#include "result.h"

struct GeneratorResult {
    Array<RuntimeStatic*> statics;

    Array<const char*> libraries;

    clock_t generator_time;
    clock_t parser_time;
};

Result<GeneratorResult> generate_ir(const char *main_file_path, Array<Statement*> main_file_statements, RegisterSize address_size, RegisterSize default_size);