#pragma once

#include "tokens.h"
#include "result.h"
#include "arena.h"
#include "array.h"

using SourceProvider = Result<Array<uint8_t>> (void* data, String path);

void register_source_provider(SourceProvider* source_provider, void* data);

Result<Array<Token>> tokenize_source(Arena* arena, String path);