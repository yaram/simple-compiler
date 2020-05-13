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

    FunctionTypeType *type;
    FunctionConstant *value;

    ResolveFunctionDeclaration() : Job { JobKind::ResolveFunctionDeclaration } {}
};

struct ResolvePolymorphicFunction : Job {
    FunctionDeclaration *declaration;
    TypedConstantValue *parameters;
    ConstantScope *scope;
    ConstantScope *call_scope;
    FileRange *call_parameter_ranges;

    FunctionTypeType *type;
    FunctionConstant *value;

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
    FunctionTypeType *type;
    FunctionConstant *value;

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