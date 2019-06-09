#pragma once

#include "parser.h"
#include "ast.h"
#include "result.h"

struct CSource {
    const char *source;

    Array<const char*> libraries;
};

Result<CSource> generate_c_source(Array<File> files);