#pragma once

#include "list.h"
#include "ast.h"
#include "constant.h"
#include "ir.h"

struct Type;
struct ConstantValue;

enum struct JobKind {
    ParseFile,
    ResolveFunctionDeclaration,
    ResolveConstantDefinition,
    ResolveStructDefinition,
    GenerateFunction,
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

struct ResolveFunctionDeclaration : Job {
    FunctionDeclaration *declaration;
    ConstantScope scope;

    Type *type;
    ConstantValue *value;

    ResolveFunctionDeclaration() : Job { JobKind::ResolveFunctionDeclaration } {}
};

struct ResolveConstantDefinition : Job {
    ConstantDefinition *definition;
    ConstantScope scope;

    Type *type;
    ConstantValue *value;

    ResolveConstantDefinition() : Job { JobKind::ResolveConstantDefinition } {}
};

struct ResolveStructDefinition : Job {
    StructDefinition *definition;
    ConstantValue **parameters;
    ConstantScope scope;

    Type *type;

    ResolveStructDefinition() : Job { JobKind::ResolveStructDefinition } {}
};

struct GenerateFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope scope;

    Function *function;
    Array<StaticConstant*> static_constants;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};

struct GenerateStaticVariable : Job {
    VariableDeclaration *declaration;
    ConstantScope scope;

    StaticVariable *static_variable;
    Type *type;
    Array<StaticConstant*> static_constants;

    GenerateStaticVariable() : Job { JobKind::GenerateStaticVariable } {}
};