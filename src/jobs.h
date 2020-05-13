#pragma once

#include "list.h"
#include "ast.h"
#include "constant.h"
#include "ir.h"

struct Type;
struct FunctionTypeType;
struct ConstantValue;

enum struct JobKind {
    ParseFile,
    ResolveFile,
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

    Array<Statement*> statements;

    ParseFile() : Job { JobKind::ParseFile } {}
};

struct ResolveFile : Job {
    ParseFile *parse_file;

    ConstantScope *scope;

    ResolveFile() : Job { JobKind::ResolveFile } {}
};

struct ResolveFunctionDeclaration : Job {
    FunctionDeclaration *declaration;
    ConstantScope *scope;

    Type *type;
    ConstantValue *value;
    ConstantScope *body_scope;
    Array<ConstantScope*> child_scopes;

    ResolveFunctionDeclaration() : Job { JobKind::ResolveFunctionDeclaration } {}
};

struct ResolvePolymorphicFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope *scope;
    ConstantScope *call_scope;
    FileRange *call_parameter_ranges;

    FunctionTypeType *type;
    ConstantScope *body_scope;
    Array<ConstantScope*> child_scopes;

    ResolvePolymorphicFunction() : Job { JobKind::ResolvePolymorphicFunction } {}
};

struct ResolveConstantDefinition : Job {
    ConstantDefinition *definition;
    ConstantScope *scope;

    Type *type;
    ConstantValue *value;

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
    ConstantValue **parameters;
    ConstantScope *scope;

    Type *type;

    ResolvePolymorphicStruct() : Job { JobKind::ResolvePolymorphicStruct } {}
};

struct GenerateFunction : Job {
    Job *resolve_function;

    Function *function;
    Array<StaticConstant*> static_constants;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};

struct GeneratePolymorphicFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope *scope;

    Function *function;
    Array<StaticConstant*> static_constants;

    GeneratePolymorphicFunction() : Job { JobKind::GeneratePolymorphicFunction } {}
};

struct GenerateStaticVariable : Job {
    VariableDeclaration *declaration;
    ConstantScope *scope;

    StaticVariable *static_variable;
    Type *type;

    GenerateStaticVariable() : Job { JobKind::GenerateStaticVariable } {}
};