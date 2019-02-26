#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "ast.h"

struct ParseSourceResult {
    bool status;

    Statement *top_level_statements;
    size_t top_level_statement_count;
};

ParseSourceResult parse_source(const char *source_file_path, FILE *source_file);