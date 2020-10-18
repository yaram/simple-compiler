#pragma once

#include "ir.h"
#include "ast.h"
#include "platform.h"

struct ConstantScope;

struct AnyType;
struct StructTypeMember;

struct FunctionTypeType {
    Array<AnyType> parameters;

    AnyType *return_type;

    CallingConvention calling_convention;
};

struct Integer {
    RegisterSize size;

    bool is_signed;
};

struct FloatType {
    RegisterSize size;
};

struct Pointer {
    AnyType *type;
};

struct ArrayTypeType {
    AnyType *element_type;
};

struct StaticArray {
    size_t length;

    AnyType *element_type;
};

struct StructType {
    StructDefinition *definition;

    Array<StructTypeMember> members;
};

struct PolymorphicStruct {
    StructDefinition *definition;

    AnyType *parameter_types;

    ConstantScope *parent;
};

struct UndeterminedStruct {
    Array<StructTypeMember> members;
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
    Type,
    Void,
    Pointer,
    ArrayTypeType,
    StaticArray,
    StructType,
    PolymorphicStruct,
    UndeterminedStruct,
    FileModule
};

struct AnyType {
    TypeKind kind;

    union {
        FunctionTypeType function;
        Integer integer;
        FloatType float_;
        Pointer pointer;
        ArrayTypeType array;
        StaticArray static_array;
        StructType struct_;
        PolymorphicStruct polymorphic_struct;
        UndeterminedStruct undetermined_struct;
    };
};

struct StructTypeMember {
    String name;

    AnyType type;
};

inline AnyType wrap_function_type(FunctionTypeType value) {
    AnyType result;
    result.kind = TypeKind::FunctionTypeType;
    result.function = value;

    return result;
}

inline AnyType create_polymorphic_function_type() {
    AnyType result;
    result.kind = TypeKind::PolymorphicFunction;

    return result;
}

inline AnyType create_builtin_function_type() {
    AnyType result;
    result.kind = TypeKind::BuiltinFunction;

    return result;
}

inline AnyType wrap_integer_type(Integer value) {
    AnyType result;
    result.kind = TypeKind::Integer;
    result.integer = value;

    return result;
}

inline AnyType create_undetermined_integer_type() {
    AnyType result;
    result.kind = TypeKind::UndeterminedInteger;

    return result;
}

inline AnyType wrap_float_type(FloatType value) {
    AnyType result;
    result.kind = TypeKind::FloatType;
    result.float_ = value;

    return result;
}

inline AnyType create_undetermined_float_type() {
    AnyType result;
    result.kind = TypeKind::UndeterminedFloat;

    return result;
}

inline AnyType create_type_type() {
    AnyType result;
    result.kind = TypeKind::Type;

    return result;
}

inline AnyType create_void_type() {
    AnyType result;
    result.kind = TypeKind::Void;

    return result;
}

inline AnyType create_boolean_type() {
    AnyType result;
    result.kind = TypeKind::Boolean;

    return result;
}

inline AnyType wrap_pointer_type(Pointer value) {
    AnyType result;
    result.kind = TypeKind::Pointer;
    result.pointer = value;

    return result;
}

inline AnyType wrap_array_type(ArrayTypeType value) {
    AnyType result;
    result.kind = TypeKind::ArrayTypeType;
    result.array = value;

    return result;
}

inline AnyType wrap_static_array_type(StaticArray value) {
    AnyType result;
    result.kind = TypeKind::StaticArray;
    result.static_array = value;

    return result;
}

inline AnyType wrap_struct_type(StructType value) {
    AnyType result;
    result.kind = TypeKind::StructType;
    result.struct_ = value;

    return result;
}

inline AnyType wrap_polymorphic_struct_type(PolymorphicStruct value) {
    AnyType result;
    result.kind = TypeKind::PolymorphicStruct;
    result.polymorphic_struct = value;

    return result;
}

inline AnyType wrap_undetermined_struct_type(UndeterminedStruct value) {
    AnyType result;
    result.kind = TypeKind::UndeterminedStruct;
    result.undetermined_struct = value;

    return result;
}

inline AnyType create_file_module_type() {
    AnyType result;
    result.kind = TypeKind::FileModule;

    return result;
}

bool types_equal(AnyType a, AnyType b);
const char *type_description(AnyType type);
bool is_runtime_type(AnyType type);
uint64_t get_type_alignment(ArchitectureSizes architecture_sizes, AnyType type);
uint64_t get_type_size(ArchitectureSizes architecture_sizes, AnyType type);
uint64_t get_struct_alignment(ArchitectureSizes architecture_sizes, StructType type);
uint64_t get_struct_size(ArchitectureSizes architecture_sizes, StructType type);
uint64_t get_struct_member_offset(ArchitectureSizes architecture_sizes, StructType type, size_t member_index);