#pragma once

#include "ast.h"
#include "hlir.h"
#include "constant.h"

struct AnyJob;

DelayedResult<void> do_generate_function(
    GlobalInfo info,
    List<AnyJob>* jobs,
    FunctionTypeType type,
    FunctionConstant value,
    Function* function
);

struct StaticVariableResult {
    StaticVariable* static_variable;

    AnyType type;
};

DelayedResult<StaticVariableResult> do_generate_static_variable(
    GlobalInfo info,
    List<AnyJob>* jobs,
    VariableDeclaration* declaration,
    ConstantScope* scope
);