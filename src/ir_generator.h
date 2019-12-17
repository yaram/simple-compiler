#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

Result<Array<Function>> generate_ir(Array<File> files);