#pragma once

#include "arena.h"
#include "ast.h"
#include "types.h"
#include "typed_tree.h"

struct ParseFile {
    String path;
    bool has_source;
    Array<uint8_t> source;

    ConstantScope* scope;
};

struct TypeStaticIf {
    StaticIf* static_if;
    ConstantScope* scope;

    bool condition;
};

struct TypeFunctionDeclaration {
    FunctionDeclaration* declaration;
    ConstantScope* scope;

    AnyType type;
    AnyConstantValue value;
};

struct TypePolymorphicFunction {
    FunctionDeclaration* declaration;
    Array<TypedConstantValue> parameters;
    ConstantScope* scope;
    ConstantScope* call_scope;
    Array<FileRange> call_parameter_ranges;

    FunctionTypeType type;
    FunctionConstant value;
};

struct TypeConstantDefinition {
    ConstantDefinition* definition;
    ConstantScope* scope;

    AnyType type;
    AnyConstantValue value;
};

struct TypeStructDefinition {
    StructDefinition* definition;
    ConstantScope* scope;

    AnyType type;
};

struct TypePolymorphicStruct {
    StructDefinition* definition;
    Array<AnyConstantValue> parameters;
    ConstantScope* scope;

    AnyType type;
};

struct TypeUnionDefinition {
    UnionDefinition* definition;
    ConstantScope* scope;

    AnyType type;
};

struct TypePolymorphicUnion {
    UnionDefinition* definition;
    Array<AnyConstantValue> parameters;
    ConstantScope* scope;

    AnyType type;
};

struct TypeEnumDefinition {
    EnumDefinition* definition;
    ConstantScope* scope;

    Enum type;
};

struct TypeFunctionBody {
    FunctionTypeType type;
    FunctionConstant value;

    Array<TypedStatement> statements;
};

struct TypeStaticVariable {
    VariableDeclaration* declaration;
    ConstantScope* scope;

    AnyType type;
};

enum struct JobKind {
    ParseFile,
    TypeStaticIf,
    TypeFunctionDeclaration,
    TypePolymorphicFunction,
    TypeConstantDefinition,
    TypeStructDefinition,
    TypePolymorphicStruct,
    TypeUnionDefinition,
    TypePolymorphicUnion,
    TypeEnumDefinition,
    TypeFunctionBody,
    TypeStaticVariable
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

    Arena arena;

    union {
        ParseFile parse_file;
        TypeStaticIf type_static_if;
        TypeFunctionDeclaration type_function_declaration;
        TypePolymorphicFunction type_polymorphic_function;
        TypeConstantDefinition type_constant_definition;
        TypeStructDefinition type_struct_definition;
        TypePolymorphicStruct type_polymorphic_struct;
        TypeUnionDefinition type_union_definition;
        TypePolymorphicUnion type_polymorphic_union;
        TypeEnumDefinition type_enum_definition;
        TypeFunctionBody type_function_body;
        TypeStaticVariable type_static_variable;
    };
};