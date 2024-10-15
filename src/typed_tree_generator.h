#pragma once

#include "list.h"
#include "ast.h"
#include "typed_tree.h"
#include "jobs.h"

struct SearchForMainResult {
    FunctionTypeType type;
    FunctionConstant value;
};

DelayedResult<SearchForMainResult> search_for_main(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    ConstantScope* scope
);

struct TypeStaticIfResult {
    TypedExpression condition;

    bool condition_value;
};

DelayedResult<TypeStaticIfResult> do_type_static_if(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    StaticIf* static_if,
    ConstantScope* scope
);

struct TypeFunctionDeclarationResult {
    Array<TypedFunctionParameter> parameters;
    Array<TypedExpression> return_types;

    AnyType type;
    AnyConstantValue value;
};

DelayedResult<TypeFunctionDeclarationResult> do_type_function_declaration(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    FunctionDeclaration* declaration,
    ConstantScope* scope
);

struct TypePolymorphicFunctionResult {
    FunctionTypeType type;
    FunctionConstant value;
};

DelayedResult<TypePolymorphicFunctionResult> do_type_polymorphic_function(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    FunctionDeclaration* declaration,
    Array<TypedConstantValue> parameters,
    ConstantScope* scope,
    ConstantScope* call_scope,
    Array<FileRange> call_parameter_ranges
);

DelayedResult<TypedExpression> do_type_constant_definition(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    ConstantDefinition* definition,
    ConstantScope* scope
);

struct TypeStructDefinitionResult {
    Array<TypedStructMember> members;

    AnyType type;
};

DelayedResult<TypeStructDefinitionResult> do_type_struct_definition(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    StructDefinition* struct_definition,
    ConstantScope* scope
);

DelayedResult<StructType> do_type_polymorphic_struct(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    StructDefinition* struct_definition,
    Array<AnyConstantValue> parameters,
    ConstantScope* scope
);

struct TypeUnionDefinitionResult {
    Array<TypedStructMember> members;

    AnyType type;
};

DelayedResult<TypeUnionDefinitionResult> do_type_union_definition(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    UnionDefinition* union_definition,
    ConstantScope* scope
);

DelayedResult<UnionType> do_type_polymorphic_union(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    UnionDefinition* union_definition,
    Array<AnyConstantValue> parameters,
    ConstantScope* scope
);

struct TypeEnumDefinitionResult {
    TypedExpression backing_type;

    Array<TypedEnumVariant> variants;

    Enum type;
};

DelayedResult<TypeEnumDefinitionResult> do_type_enum_definition(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* arena,
    Arena* global_arena,
    EnumDefinition* enum_definition,
    ConstantScope* scope
);

struct TypeFunctionBodyResult {
    VariableScope* scope;
    Array<TypedStatement> statements;
};

DelayedResult<TypeFunctionBodyResult> do_type_function_body(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    FunctionTypeType type,
    FunctionConstant value
);

struct TypeStaticVariableResult {
    bool is_external;

    TypedExpression type;
    TypedExpression initializer;

    AnyType actual_type;

    Array<String> external_libraries;
};

DelayedResult<TypeStaticVariableResult> do_type_static_variable(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    VariableDeclaration* declaration,
    ConstantScope* scope
);

DelayedResult<TypedExpression> do_type_using(
    GlobalInfo info,
    List<AnyJob*>* jobs,
    Arena* global_arena,
    Arena* arena,
    UsingStatement* statement,
    ConstantScope* scope
);

Result<void> process_scope(
    Arena* global_arena,
    List<AnyJob*>* jobs,
    ConstantScope* scope,
    Array<Statement*> statements,
    List<ConstantScope*>* child_scopes,
    bool is_top_level
);