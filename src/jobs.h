#pragma once

#include "list.h"
#include "ast.h"
#include "constant.h"
#include "ir.h"

enum struct JobKind {
    ParseFile,
    ResolveDeclaration,
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

struct ResolveDeclaration : Job {
    Statement *declaration;
    Array<ConstantParameter> parameters;
    ConstantScope *scope;

    Type *type;
    ConstantValue *value;

    ResolveDeclaration() : Job { JobKind::ResolveDeclaration } {}
};

struct GenerateFunction : Job {
    FunctionDeclaration *declaration;
    const char *name;
    Array<ConstantParameter> constant_parameters;
    ConstantScope *scope;

    Function *function;
    Array<StaticConstant*> static_constants;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};

struct GenerateStaticVariable : Job {
    VariableDeclaration *declaration;
    const char *name;
    ConstantScope *scope;

    StaticVariable *static_variable;
    Array<StaticConstant*> static_constants;

    GenerateStaticVariable() : Job { JobKind::GenerateStaticVariable } {}
};