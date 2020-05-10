#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct Job;

struct GeneratorResult {
    Function *function;

    Array<StaticConstant*> static_constants;
};

Result<DelayedValue<GeneratorResult>> do_generate_function(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *declaration,
    TypedConstantValue *parameters,
    ConstantScope scope
);