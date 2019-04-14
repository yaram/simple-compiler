#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "ast.h"
#include "result.h"

Result<Array<Statement>> parse_source(const char *source_file_path, FILE *source_file);