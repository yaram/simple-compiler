#pragma once

#include "ast.h"
#include "result.h"

Result<char*> generate_c_source(Array<Statement> top_level_statements);