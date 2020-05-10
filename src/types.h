#pragma once

#include "constant.h"

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

bool types_equal(Type *a, Type *b);
const char *type_description(Type *type);
bool is_runtime_type(Type *type);
uint64_t get_type_alignment(GlobalInfo info, Type *type);
uint64_t get_struct_alignment(GlobalInfo info, StructType type);
uint64_t get_type_alignment(GlobalInfo info, Type *type);
uint64_t get_type_size(GlobalInfo info, Type *type);
uint64_t get_struct_size(GlobalInfo info, StructType type);
uint64_t get_type_size(GlobalInfo info, Type *type);
uint64_t get_struct_member_offset(GlobalInfo info, StructType type, size_t member_index);