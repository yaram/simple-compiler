#pragma once

#include "ast.h"
#include "ir.h"
#include "types.h"
#include "result.h"

struct PolymorphicDeterminer {
    const char *name;

    Type type;
};

struct DeterminedDeclaration {
    Statement declaration;

    Array<PolymorphicDeterminer> polymorphic_determiners;

    DeterminedDeclaration *parent;
};

union ConstantValue {
    struct {
        Statement declaration;

        DeterminedDeclaration parent;

        const char *file_path;
    } function;

    uint64_t integer;

    bool boolean;

    Type type;

    size_t pointer;

    struct {
        size_t length;

        size_t pointer;
    } array;

    ConstantValue *static_array;

    ConstantValue *struct_;

    struct {
        const char *path;

        Array<Statement> statements;
    } file_module;
};

struct TypedConstantValue {
    Type type;

    ConstantValue value;
};

struct RuntimeFunctionParameter {
    Identifier name;

    Type type;
    FileRange type_range;
};

struct RuntimeFunction {
    const char *mangled_name;

    Array<RuntimeFunctionParameter> parameters;

    Type return_type;

    Statement declaration;

    DeterminedDeclaration parent;

    const char *file_path;

    Array<PolymorphicDeterminer> polymorphic_determiners;
};

enum struct ValueCategory {
    Constant,
    Anonymous,
    Address
};

struct Value {
    ValueCategory category;

    union {
        struct {
            size_t register_;

            Value *undetermined_struct;
        } anonymous;

        size_t address;

        ConstantValue constant;
    };
};

struct TypedValue {
    Type type;

    Value value;
};

struct IR {
    Array<Function> functions;

    Array<const char*> libraries;

    Array<StaticConstant> constants;
};

Result<IR> generate_ir(const char *main_file_path, Array<Statement> main_file_statements, RegisterSize address_size, RegisterSize default_size);