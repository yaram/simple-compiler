#pragma once

#include "tokens.h"
#include "result.h"
#include "array.h"

Result<Array<Token>> tokenize_source(const char *path);