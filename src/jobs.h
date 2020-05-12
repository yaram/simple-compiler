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

    bool being_worked_on;
};

struct ParseFile : Job {
    const char *path;

    ConstantScope *scope;

    ParseFile() : Job { JobKind::ParseFile } {}
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

struct ResolveConstantDefinition : Job {
    ConstantDefinition *definition;
    ConstantScope *scope;

    Type *type;
    ConstantValue *value;

    ResolveConstantDefinition() : Job { JobKind::ResolveConstantDefinition } {}
};

struct ResolveStructDefinition : Job {
    StructDefinition *definition;
    ConstantValue **parameters;
    ConstantScope *scope;

    Type *type;

    ResolveStructDefinition() : Job { JobKind::ResolveStructDefinition } {}
};

struct GenerateFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope *scope;
    ConstantScope *body_scope;
    Array<ConstantScope*> child_scopes;

    Function *function;
    Array<StaticConstant*> static_constants;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};

struct GenerateStaticVariable : Job {
    VariableDeclaration *declaration;
    ConstantScope *scope;

    StaticVariable *static_variable;
    Type *type;

    GenerateStaticVariable() : Job { JobKind::GenerateStaticVariable } {}
};