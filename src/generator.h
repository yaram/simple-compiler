#pragma once

#include "ast.h"
#include "ir.h"
#include "result.h"

struct Job;

Result<DelayedValue<Array<StaticConstant*>>> do_generate_function(
    GlobalInfo info,
    List<Job*> *jobs,
    Job *resolve_function,
    Function *function
);

struct StaticVariableResult {
    StaticVariable *static_variable;

    Type *type;
};

Result<DelayedValue<StaticVariableResult>> do_generate_static_variable(
    GlobalInfo info,
    List<Job*> *jobs,
    VariableDeclaration *declaration,
    ConstantScope *scope
);