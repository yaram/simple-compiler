#pragma once

#include "arena.h"
#include "ast.h"
#include "types.h"
#include "typed_tree.h"

struct ParseFile {
    String path;

    ConstantScope* scope;
};

struct TypeStaticIf {
    StaticIf* static_if;
    ConstantScope* scope;

    TypedExpression condition;
    bool condition_value;
};

struct TypeFunctionDeclaration {
    FunctionDeclaration* declaration;
    ConstantScope* scope;

    Array<TypedFunctionParameter> parameters;
    Array<TypedExpression> return_types;
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

    TypedExpression value;
};

struct TypeStructDefinition {
    StructDefinition* definition;
    ConstantScope* scope;

    Array<TypedStructMember> members;
    AnyType type;
};

struct TypePolymorphicStruct {
    StructDefinition* definition;
    Array<AnyConstantValue> parameters;
    ConstantScope* scope;

    StructType type;
};

struct TypeUnionDefinition {
    UnionDefinition* definition;
    ConstantScope* scope;

    Array<TypedStructMember> members;
    AnyType type;
};

struct TypePolymorphicUnion {
    UnionDefinition* definition;
    Array<AnyConstantValue> parameters;
    ConstantScope* scope;

    UnionType type;
};

struct TypeEnumDefinition {
    EnumDefinition* definition;
    ConstantScope* scope;

    TypedExpression backing_type;
    Array<TypedEnumVariant> variants;
    Enum type;
};

struct TypeFunctionBody {
    FunctionTypeType type;
    FunctionConstant value;

    VariableScope* scope;
    Array<TypedStatement> statements;
};

struct TypeStaticVariable {
    VariableDeclaration* declaration;
    ConstantScope* scope;

    bool is_external;
    TypedExpression type;
    TypedExpression initializer;
    AnyType actual_type;
    Array<String> external_libraries;
};

struct TypeUsing {
    UsingStatement* statement;
    ConstantScope* scope;

    TypedExpression value;
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
    TypeStaticVariable,
    TypeUsing
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
        TypeUsing type_using;
    };
};

template <typename T>
struct DelayedResult {
    inline DelayedResult() {}

    inline DelayedResult(Result<T> result) {
        has_value = true;
        status = result.status;
        value = result.value;
    }

    inline DelayedResult(ResultErrorHelper helper) {
        has_value = true;
        status = false;
    }

    bool has_value;

    union {
        struct {
            bool status;
            T value;
        };

        size_t waiting_for;
    };
};

template <>
struct DelayedResult<void> {
    inline DelayedResult() {}

    inline DelayedResult(Result<void> result) {
        has_value = true;
        status = result.status;
    }

    inline DelayedResult(ResultErrorHelper helper) {
        has_value = true;
        status = false;
    }

    bool has_value;

    union {
        bool status;

        size_t waiting_for;
    };
};

struct DelayedResultWaitHelper {
    size_t waiting_for;

    template <typename T>
    inline operator DelayedResult<T>() {
        DelayedResult<T> result;
        result.has_value = false;
        result.waiting_for = waiting_for;
        return result;
    }
};

inline DelayedResultWaitHelper wait(size_t job) {
    DelayedResultWaitHelper helper {};
    helper.waiting_for = job;

    return helper;
}

#define expect_delayed(name, expression) auto __##name##_result=(expression);if(__##name##_result.has_value){if(!__##name##_result.status){return err();}}else return(wait(__##name##_result.waiting_for));auto name = __##name##_result.value
#define expect_delayed_void(expression) auto __void_result=(expression);if(__void_result.has_value){if(!__void_result.status){return err();}}else return(wait(__void_result.waiting_for))