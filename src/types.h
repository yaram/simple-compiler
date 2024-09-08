#pragma once

#include "ir.h"
#include "ast.h"
#include "platform.h"

struct ConstantScope;

struct AnyType;
struct StructTypeMember;

struct FunctionTypeType {
    inline FunctionTypeType() = default;
    inline FunctionTypeType(Array<AnyType> parameters, AnyType* return_type, CallingConvention calling_convention) :
        parameters(parameters), return_type(return_type), calling_convention(calling_convention)
    {}

    Array<AnyType> parameters;

    AnyType* return_type;

    CallingConvention calling_convention;
};

struct Integer {
    inline Integer() = default;
    inline Integer(RegisterSize size, bool is_signed) : size(size), is_signed(is_signed) {}

    RegisterSize size;

    bool is_signed;
};

struct FloatType {
    inline FloatType() = default;
    inline FloatType(RegisterSize size) : size(size) {}

    RegisterSize size;
};

struct Pointer {
    inline Pointer() = default;
    inline Pointer(AnyType* type) : type(type) {}

    AnyType* type;
};

struct ArrayTypeType {
    inline ArrayTypeType() = default;
    inline ArrayTypeType(AnyType* element_type) : element_type(element_type) {}

    AnyType* element_type;
};

struct StaticArray {
    inline StaticArray() = default;
    inline StaticArray(size_t length, AnyType* element_type) : length(length), element_type(element_type) {}

    size_t length;

    AnyType* element_type;
};

struct StructType {
    inline StructType() = default;
    inline StructType(StructDefinition* definition, Array<StructTypeMember> members) : definition(definition), members(members) {}

    StructDefinition* definition;

    Array<StructTypeMember> members;

    uint64_t get_alignment(ArchitectureSizes architecture_sizes);
    uint64_t get_size(ArchitectureSizes architecture_sizes);
    uint64_t get_member_offset(ArchitectureSizes architecture_sizes, size_t member_index);
};

struct PolymorphicStruct {
    inline PolymorphicStruct() = default;
    inline PolymorphicStruct(StructDefinition* definition, AnyType* parameter_types, ConstantScope* parent) :
        definition(definition), parameter_types(parameter_types), parent(parent)
    {}

    StructDefinition* definition;

    AnyType* parameter_types;

    ConstantScope* parent;
};

struct UndeterminedStruct {
    inline UndeterminedStruct() = default;
    inline UndeterminedStruct(Array<StructTypeMember> members) : members(members) {}

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

    bool operator==(AnyType other);
    bool operator!=(AnyType other);
    String get_description();
    bool is_runtime_type();
    uint64_t get_alignment(ArchitectureSizes architecture_sizes);
    uint64_t get_size(ArchitectureSizes architecture_sizes);
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