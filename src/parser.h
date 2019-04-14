#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "ast.h"

struct ParseSourceResult {
    bool status;

    Array<Statement> top_level_statements;
};

ParseSourceResult parse_source(const char *source_file_path, FILE *source_file);