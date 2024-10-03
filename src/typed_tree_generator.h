#pragma once

#include "ast.h"
#include "typed_tree.h"
#include "constant.h"

struct AnyJob;

DelayedResult<Array<TypedStatement>> do_type_function_body(
    GlobalInfo info,
    List<AnyJob>* jobs,
    Arena* arena,
    FunctionTypeType type,
    FunctionConstant value
);

DelayedResult<AnyType> do_type_static_variable(
    GlobalInfo info,
    List<AnyJob>* jobs,
    Arena* arena,
    VariableDeclaration* declaration,
    ConstantScope* scope
);