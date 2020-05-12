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
    Mutex *jobs_mutex,
    FunctionDeclaration *declaration,
    TypedConstantValue *parameters,
    ConstantScope *scope,
    ConstantScope *body_scope,
    Array<ConstantScope*> child_scopes
);

struct StaticVariableResult {
    StaticVariable *static_variable;

    Type *type;
};

Result<DelayedValue<StaticVariableResult>> do_generate_static_variable(
    GlobalInfo info,
    List<Job*> *jobs,
    Mutex *jobs_mutex,
    VariableDeclaration *declaration,
    ConstantScope *scope
);