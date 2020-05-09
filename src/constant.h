#pragma once

#include <assert.h>
#include "result.h"
#include "ast.h"
#include "register_size.h"
#include "list.h"

struct Job;
struct Type;
struct ConstantValue;

struct ConstantParameter {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct ConstantScope {
    Array<Statement*> statements;

    Array<ConstantParameter> constant_parameters;

    bool is_top_level;

    ConstantScope *parent;

    const char *file_path;
};

enum struct TypeKind {
    FunctionTypeType,
    PolymorphicFunction,
    BuiltinFunction,
    Integer,
    UndeterminedInteger,
    Boolean,
    FloatType,
    UndeterminedFloat,
    TypeType,
    Void,
    Pointer,
    ArrayTypeType,
    StaticArray,
    StructType,
    PolymorphicStruct,
    UndeterminedStruct,
    FileModule
};

struct Type {
    TypeKind kind;

    Type(TypeKind kind) : kind { kind } {}
};

extern Type polymorphic_function_singleton;
extern Type builtin_function_singleton;
extern Type undetermined_integer_singleton;
extern Type boolean_singleton;
extern Type undetermined_float_singleton;
extern Type type_type_singleton;
extern Type void_singleton;
extern Type file_module_singleton;

struct FunctionTypeType : Type {
    Array<Type*> parameters;

    Type *return_type;

    FunctionTypeType(
        Array<Type*> parameters,
        Type *return_type
    ) :
        Type { TypeKind::FunctionTypeType },
        parameters { parameters },
        return_type { return_type }
    {}
};

struct Integer : Type {
    RegisterSize size;

    bool is_signed;

    Integer(
        RegisterSize size,
        bool is_signed
    ) :
        Type { TypeKind::Integer },
        size { size },
        is_signed { is_signed }
    {}
};

struct FloatType : Type {
    RegisterSize size;

    FloatType(
        RegisterSize size
    ) :
        Type { TypeKind::FloatType },
        size { size }
    {}
};

struct Pointer : Type {
    Type *type;

    Pointer(
        Type *type
    ) :
        Type { TypeKind::Pointer },
        type { type }
    {}
};

struct ArrayTypeType : Type {
    Type *element_type;

    ArrayTypeType(
        Type *element_type
    ) :
        Type { TypeKind::ArrayTypeType },
        element_type { element_type }
    {}
};

struct StaticArray : Type {
    size_t length;

    Type *element_type;

    StaticArray(
        size_t length,
        Type *element_type
    ) :
        Type { TypeKind::StaticArray },
        length { length },
        element_type { element_type }
    {}
};

struct StructType : Type {
    struct Member {
        const char *name;

        Type *type;
    };

    StructDefinition *definition;

    Array<Member> members;

    StructType(
        StructDefinition *definition,
        Array<Member> members
    ) :
        Type { TypeKind::StructType },
        definition { definition },
        members { members }
    {}
};

struct PolymorphicStruct : Type {
    StructDefinition *definition;

    Type **parameter_types;

    ConstantScope parent;

    PolymorphicStruct(
        StructDefinition *definition,
        Type **parameter_types,
        ConstantScope parent
    ) :
        Type { TypeKind::PolymorphicStruct },
        definition { definition },
        parameter_types { parameter_types },
        parent { parent }
    {}
};

struct UndeterminedStruct : Type {
    struct Member {
        const char *name;

        Type *type;
    };

    Array<Member> members;

    UndeterminedStruct(
        Array<Member> members
    ) :
        Type { TypeKind::UndeterminedStruct },
        members { members }
    {}
};

struct GlobalConstant {
    const char *name;

    Type *type;

    ConstantValue *value;
};

struct GlobalInfo {
    Array<GlobalConstant> global_constants;

    RegisterSize address_integer_size;
    RegisterSize default_integer_size;
};

enum struct ConstantValueKind {
    FunctionConstant,
    BuiltinFunctionConstant,
    IntegerConstant,
    FloatConstant,
    BooleanConstant,
    VoidConstant,
    PointerConstant,
    ArrayConstant,
    StaticArrayConstant,
    StructConstant,
    FileModuleConstant,
    TypeConstant
};

struct ConstantValue {
    ConstantValueKind kind;
};

extern ConstantValue void_constant_singleton;

struct FunctionConstant : ConstantValue {
    FunctionDeclaration *declaration;

    ConstantScope parent;

    FunctionConstant(
        FunctionDeclaration *declaration,
        ConstantScope parent
    ) :
    ConstantValue { ConstantValueKind::FunctionConstant },
    declaration { declaration },
    parent { parent }
    {}
};

struct BuiltinFunctionConstant : ConstantValue {
    const char *name;

    BuiltinFunctionConstant(
        const char *name
    ) :
        ConstantValue { ConstantValueKind::BuiltinFunctionConstant },
        name { name }
    {}
};

struct IntegerConstant : ConstantValue {
    uint64_t value;

    IntegerConstant(
        uint64_t value
    ) :
        ConstantValue { ConstantValueKind::IntegerConstant },
        value { value }
    {}
};

struct FloatConstant : ConstantValue {
    double value;

    FloatConstant(
        double value
    ) :
        ConstantValue { ConstantValueKind::FloatConstant },
        value { value }
    {}
};

struct BooleanConstant : ConstantValue {
    bool value;

    BooleanConstant(
        bool value
    ) :
        ConstantValue { ConstantValueKind::BooleanConstant },
        value { value }
    {}
};

struct PointerConstant : ConstantValue {
    uint64_t value;

    PointerConstant(
        uint64_t value
    ) :
        ConstantValue { ConstantValueKind::PointerConstant },
        value { value }
    {}
};

struct ArrayConstant : ConstantValue {
    uint64_t length;

    uint64_t pointer;

    ArrayConstant(
        uint64_t length,
        uint64_t pointer
    ) :
        ConstantValue { ConstantValueKind::ArrayConstant },
        length { length },
        pointer { pointer }
    {}
};

struct StaticArrayConstant : ConstantValue {
    ConstantValue **elements;

    StaticArrayConstant(
        ConstantValue **elements
    ) :
        ConstantValue { ConstantValueKind::StaticArrayConstant },
        elements { elements }
    {}
};

struct StructConstant : ConstantValue {
    ConstantValue **members;

    StructConstant(
        ConstantValue **members
    ) :
        ConstantValue { ConstantValueKind::StructConstant },
        members { members }
    {}
};

struct FileModuleConstant : ConstantValue {
    const char *path;

    Array<Statement*> statements;

    FileModuleConstant(
        const char *path,
        Array<Statement*> statements
    ) :
        ConstantValue { ConstantValueKind::FileModuleConstant },
        path { path },
        statements { statements }
    {}
};

struct TypeConstant : ConstantValue {
    Type *type;

    TypeConstant(
        Type *type
    ) :
        ConstantValue { ConstantValueKind::TypeConstant },
        type { type }
    {}
};

#define extract_constant_value(kind, value) extract_constant_value_internal<kind>(value, ConstantValueKind::kind)

template <typename T>
T *extract_constant_value_internal(ConstantValue *value, ConstantValueKind kind) {
    assert(value->kind == kind);

    auto extracted_value = (T*)value;

    return extracted_value;
}

struct TypedConstantValue {
    Type *type;

    ConstantValue *value;
};

template <typename T>
struct DelayedValue {
    bool has_value;

    T value;
    Job *waiting_for;
};

#define expect_delayed(name, expression) expect(__##name##_delayed, expression);if(!__##name##_delayed.has_value)return{true,{false,{},__##name##_delayed.waiting_for}};auto name=__##name##_delayed.value;

void error(ConstantScope scope, FileRange range, const char *format, ...);

Result<DelayedValue<TypedConstantValue>> evaluate_constant_expression(
    GlobalInfo info,
    List<Job*> *jobs,
    ConstantScope scope,
    Expression *expression
);

Result<DelayedValue<TypedConstantValue>> do_resolve_function_declaration(
    GlobalInfo info,
    List<Job*> *jobs,
    FunctionDeclaration *function_declaration,
    ConstantScope scope
);

Result<DelayedValue<Type*>> do_resolve_struct_definition(
    GlobalInfo info,
    List<Job*> *jobs,
    StructDefinition *struct_definition,
    ConstantValue **parameters,
    ConstantScope scope
);