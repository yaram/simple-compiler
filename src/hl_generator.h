#pragma once

#include "ast.h"
#include "typed_tree.h"
#include "hlir.h"

struct TypedFunction {
    FunctionTypeType type;
    FunctionConstant constant;

    Function* function;
};

struct TypedStaticVariable {
    AnyType type;
    ConstantScope* scope;
    VariableDeclaration* declaration;

    StaticVariable* static_variable;
};

struct AnyJob;

Array<StaticConstant*> do_generate_function(
    GlobalInfo info,
    Array<TypedFunction> functions,
    Array<TypedStaticVariable> static_variables,
    Arena* arena,
    FunctionTypeType type,
    FunctionConstant value,
    Array<TypedStatement> statements,
    Function* function
);

StaticVariable* do_generate_static_variable(
    GlobalInfo info,
    Array<TypedFunction> functions,
    Array<TypedStaticVariable> static_variables,
    Arena* arena,
    VariableDeclaration* declaration,
    ConstantScope* scope,
    bool is_external,
    bool is_no_mangle,
    TypedExpression type,
    TypedExpression initializer,
    AnyType actual_type,
    Array<String> external_libraries
);