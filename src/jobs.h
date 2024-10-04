#pragma once

#include "arena.h"
#include "ast.h"
#include "constant.h"
#include "types.h"
#if defined(SERVER)
#include "typed_tree.h"
#else
#include "hlir.h"
#endif

struct ParseFile {
    String path;
    bool has_source;
    Array<uint8_t> source;

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

struct ResolveUnionDefinition {
    UnionDefinition* definition;
    ConstantScope* scope;

    AnyType type;
};

struct ResolvePolymorphicUnion {
    UnionDefinition* definition;
    AnyConstantValue* parameters;
    ConstantScope* scope;

    AnyType type;
};

struct ResolveEnumDefinition {
    EnumDefinition* definition;
    ConstantScope* scope;

    Enum type;
};

#if defined(SERVER)

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

#else

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

#endif

enum struct JobKind {
    ParseFile,
    ResolveStaticIf,
    ResolveFunctionDeclaration,
    ResolvePolymorphicFunction,
    ResolveConstantDefinition,
    ResolveStructDefinition,
    ResolvePolymorphicStruct,
    ResolveUnionDefinition,
    ResolvePolymorphicUnion,
    ResolveEnumDefinition,
#if defined(SERVER)
    TypeFunctionBody,
    TypeStaticVariable
#else
    GenerateFunction,
    GenerateStaticVariable
#endif
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
        ResolveStaticIf resolve_static_if;
        ResolveFunctionDeclaration resolve_function_declaration;
        ResolvePolymorphicFunction resolve_polymorphic_function;
        ResolveConstantDefinition resolve_constant_definition;
        ResolveStructDefinition resolve_struct_definition;
        ResolvePolymorphicStruct resolve_polymorphic_struct;
        ResolveUnionDefinition resolve_union_definition;
        ResolvePolymorphicUnion resolve_polymorphic_union;
        ResolveEnumDefinition resolve_enum_definition;
#if defined(SERVER)
        TypeFunctionBody type_function_body;
        TypeStaticVariable type_static_variable;
#else
        GenerateFunction generate_function;
        GenerateStaticVariable generate_static_variable;
#endif
    };
};