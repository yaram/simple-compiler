#pragma once

#include "result.h"
#include "array.h"
#include "arena.h"
#include "tokens.h"
#include "ast.h"

Result<Array<Statement*>> parse_tokens(Arena* arena, String path, Array<Token> tokens);