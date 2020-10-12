#pragma once

#include "ast.h"
#include "constant.h"
#include "ir.h"

struct Type;
struct FunctionTypeType;
struct ConstantValue;
struct FunctionConstant;

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

struct Job {
    JobKind kind;

    bool done;
    Job* waiting_for;
};

struct ParseFile : Job {
    const char *path;

    ConstantScope *scope;

    ParseFile() : Job { JobKind::ParseFile } {}
};

struct ResolveStaticIf : Job {
    StaticIf *static_if;
    ConstantScope *scope;

    bool condition;
    DeclarationHashTable declarations;

    ResolveStaticIf() : Job { JobKind::ResolveStaticIf } {}
};

struct ResolveFunctionDeclaration : Job {
    FunctionDeclaration *declaration;
    ConstantScope *scope;

    Type *type;
    AnyConstantValue value;

    ResolveFunctionDeclaration() : Job { JobKind::ResolveFunctionDeclaration } {}
};

struct ResolvePolymorphicFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope *scope;
    ConstantScope *call_scope;
    FileRange *call_parameter_ranges;

    FunctionTypeType *type;
    FunctionConstant value;

    ResolvePolymorphicFunction() : Job { JobKind::ResolvePolymorphicFunction } {}
};

struct ResolveConstantDefinition : Job {
    ConstantDefinition *definition;
    ConstantScope *scope;

    Type *type;
    AnyConstantValue value;

    ResolveConstantDefinition() : Job { JobKind::ResolveConstantDefinition } {}
};

struct ResolveStructDefinition : Job {
    StructDefinition *definition;
    ConstantScope *scope;

    Type *type;

    ResolveStructDefinition() : Job { JobKind::ResolveStructDefinition } {}
};

struct ResolvePolymorphicStruct : Job {
    StructDefinition *definition;
    AnyConstantValue *parameters;
    ConstantScope *scope;

    Type *type;

    ResolvePolymorphicStruct() : Job { JobKind::ResolvePolymorphicStruct } {}
};

struct GenerateFunction : Job {
    FunctionTypeType *type;
    FunctionConstant value;

    Function *function;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};

struct GenerateStaticVariable : Job {
    VariableDeclaration *declaration;
    ConstantScope *scope;

    StaticVariable *static_variable;
    Type *type;

    GenerateStaticVariable() : Job { JobKind::GenerateStaticVariable } {}
};