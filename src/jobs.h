#pragma once

#include "list.h"
#include "ast.h"
#include "constant.h"
#include "ir.h"

enum struct JobKind {
    ParseFile,
    ResolveDeclaration,
    GenerateFunction
};

struct Job {
    JobKind kind;

    bool done;
};

struct ParseFile : Job {
    const char *path;

    Array<Statement*> statements;

    ParseFile() : Job { JobKind::ParseFile } {}
};

struct ResolveDeclaration : Job {
    FunctionDeclaration *declaration;
    Array<ConstantParameter> parameters;

    Type *type;
    ConstantValue *value;

    ResolveDeclaration() : Job { JobKind::ResolveDeclaration } {}
};

struct GenerateFunction : Job {
    FunctionDeclaration *declaration;
    const char *name;
    Array<ConstantParameter> constant_parameters;

    Array<Instruction*> instructions;

    GenerateFunction() : Job { JobKind::GenerateFunction } {}
};