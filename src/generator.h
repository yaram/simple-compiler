#pragma once

#include "ast.h"

struct GenerateCSourceResult {
    bool status;

    char *source;
};

GenerateCSourceResult generate_c_source(Statement *top_level_statements, size_t top_level_statement_count);