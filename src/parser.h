#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "ast.h"
#include "result.h"

struct File {
    const char *path;

    Array<Statement> statements;
};

Result<Array<File>> parse_source(const char *source_file_path);