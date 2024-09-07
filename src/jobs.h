#pragma once

#include "ast.h"
#include "constant.h"
#include "types.h"
#include "ir.h"

struct ParseFile {
    String path;

    ConstantScope* scope;
};

struct ResolveStaticIf {
    StaticIf* static_if;
    ConstantScope* scope;

    bool condition;
    DeclarationHashTable declarations;
};

struct ResolveFunctionDeclaration {
    FunctionDeclaration* declaration;
    ConstantScope* scope;

    AnyType type;
    AnyConstantValue value;
};

struct ResolvePolymorphicFunction {
    FunctionDeclaration* declaration;
    TypedConstantValue* parameters;
    ConstantScope* scope;
    ConstantScope* call_scope;
    FileRange* call_parameter_ranges;

    FunctionTypeType type;
    FunctionConstant value;
};

struct ResolveConstantDefinition {
    ConstantDefinition* definition;
    ConstantScope* scope;

    AnyType type;
    AnyConstantValue value;
};

struct ResolveStructDefinition {
    StructDefinition* definition;
    ConstantScope* scope;

    AnyType type;
};

struct ResolvePolymorphicStruct {
    StructDefinition* definition;
    AnyConstantValue* parameters;
    ConstantScope* scope;

    AnyType type;
};

struct GenerateFunction {
    FunctionTypeType type;
    FunctionConstant value;

    Function* function;
};

struct GenerateStaticVariable {
    VariableDeclaration* declaration;
    ConstantScope* scope;

    StaticVariable* static_variable;
    AnyType type;
};

enum struct JobKind {
    ParseFile,
    ResolveStaticIf,
    ResolveFunctionDeclaration,
    ResolvePolymorphicFunction,
    ResolveConstantDefinition,
    ResolveStructDefinition,
    ResolvePolymorphicStruct,
    GenerateFunction,
    GeneratePolymorphicFunction,
    GenerateStaticVariable
};

enum struct JobState {
    Working,
    Waiting,
    Done
};

struct AnyJob {
    JobKind kind;
    
    JobState state;

    size_t waiting_for;

    union {
        ParseFile parse_file;
        ResolveStaticIf resolve_static_if;
        ResolveFunctionDeclaration resolve_function_declaration;
        ResolvePolymorphicFunction resolve_polymorphic_function;
        ResolveConstantDefinition resolve_constant_definition;
        ResolveStructDefinition resolve_struct_definition;
        ResolvePolymorphicStruct resolve_polymorphic_struct;
        GenerateFunction generate_function;
        GenerateStaticVariable generate_static_variable;
    };
};