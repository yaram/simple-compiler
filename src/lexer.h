#pragma once

#include "tokens.h"
#include "result.h"
#include "arena.h"
#include "array.h"

Result<Array<Token>> tokenize_source(Arena* arena, String path);