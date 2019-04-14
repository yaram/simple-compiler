#pragma once

#include "ast.h"

struct GenerateCSourceResult {
    bool status;

    char *source;
};

GenerateCSourceResult generate_c_source(Array<Statement> top_level_statements);