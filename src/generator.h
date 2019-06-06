#pragma once

#include "parser.h"
#include "ast.h"
#include "result.h"

Result<char*> generate_c_source(Array<File> files);