#pragma once

#include "result.h"
#include "array.h"
#include "tokens.h"
#include "ast.h"

Result<Array<Statement*>> parse_tokens(String path, Array<Token> tokens);