#pragma once

#include "calling_convention.h"
#include "ast.h"
#include "platform.h"

struct ConstantScope;

struct AnyType;
struct StructTypeMember;

struct FunctionTypeType {
    inline FunctionTypeType() = default;
    explicit inline FunctionTypeType(Array<AnyType> parameters, AnyType* return_type, CallingConvention calling_convention) :
        parameters(parameters), return_type(return_type), calling_convention(calling_convention)
    {}

    Array<AnyType> parameters;

    AnyType* return_type;

    CallingConvention calling_convention;
};

struct Integer {
    inline Integer() = default;
    explicit inline Integer(RegisterSize size, bool is_signed) : size(size), is_signed(is_signed) {}

    RegisterSize size;

    bool is_signed;
};

struct FloatType {
    inline FloatType() = default;
    explicit inline FloatType(RegisterSize size) : size(size) {}

    RegisterSize size;
};

struct Pointer {
    inline Pointer() = default;
    explicit inline Pointer(AnyType* type) : pointed_to_type(type) {}

    AnyType* pointed_to_type;
};

struct ArrayTypeType {
    inline ArrayTypeType() = default;
    explicit inline ArrayTypeType(AnyType* element_type) : element_type(element_type) {}

    AnyType* element_type;
};

struct StaticArray {
    inline StaticArray() = default;
    explicit inline StaticArray(size_t length, AnyType* element_type) : length(length), element_type(element_type) {}

    size_t length;

    AnyType* element_type;
};

struct StructType {
    inline StructType() = default;
    explicit inline StructType(
        String definition_file_path,
        StructDefinition* definition,
        Array<StructTypeMember> members
    ) :
        definition_file_path(definition_file_path),
        definition(definition),
        members(members)
    {}

    String definition_file_path;
    StructDefinition* definition;

    Array<StructTypeMember> members;

    uint64_t get_alignment(ArchitectureSizes architecture_sizes);
    uint64_t get_size(ArchitectureSizes architecture_sizes);
    uint64_t get_member_offset(ArchitectureSizes architecture_sizes, size_t member_index);
};

struct PolymorphicStruct {
    inline PolymorphicStruct() = default;
    explicit inline PolymorphicStruct(
        String definition_file_path,
        StructDefinition* definition,
        AnyType* parameter_types,
        ConstantScope* parent
    ) :
        definition_file_path(definition_file_path),
        definition(definition),
        parameter_types(parameter_types),
        parent(parent)
    {}

    String definition_file_path;
    StructDefinition* definition;

    AnyType* parameter_types;

    ConstantScope* parent;
};

struct UnionType {
    inline UnionType() = default;
    explicit inline UnionType(
        String definition_file_path,
        UnionDefinition* definition,
        Array<StructTypeMember> members
    ) :
        definition_file_path(definition_file_path),
        definition(definition),
        members(members)
    {}

    String definition_file_path;
    UnionDefinition* definition;

    Array<StructTypeMember> members;

    uint64_t get_alignment(ArchitectureSizes architecture_sizes);
    uint64_t get_size(ArchitectureSizes architecture_sizes);
};

struct PolymorphicUnion {
    inline PolymorphicUnion() = default;
    explicit inline PolymorphicUnion(
        String definition_file_path,
        UnionDefinition* definition,
        AnyType* parameter_types,
        ConstantScope* parent
    ) :
        definition_file_path(definition_file_path),
        definition(definition),
        parameter_types(parameter_types),
        parent(parent)
    {}

    String definition_file_path;
    UnionDefinition* definition;

    AnyType* parameter_types;

    ConstantScope* parent;
};

struct UndeterminedStruct {
    inline UndeterminedStruct() = default;
    explicit inline UndeterminedStruct(Array<StructTypeMember> members) : members(members) {}

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
    UnionType,
    PolymorphicUnion,
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
        UnionType union_;
        PolymorphicUnion polymorphic_union;
        UndeterminedStruct undetermined_struct;
    };

    inline AnyType() = default;
    explicit inline AnyType(FunctionTypeType function) : kind(TypeKind::FunctionTypeType), function(function) {}
    explicit inline AnyType(Integer integer) : kind(TypeKind::Integer), integer(integer) {}
    explicit inline AnyType(FloatType float_) : kind(TypeKind::FloatType), float_(float_) {}
    explicit inline AnyType(Pointer pointer) : kind(TypeKind::Pointer), pointer(pointer) {}
    explicit inline AnyType(ArrayTypeType array) : kind(TypeKind::ArrayTypeType), array(array) {}
    explicit inline AnyType(StaticArray static_array) : kind(TypeKind::StaticArray), static_array(static_array) {}
    explicit inline AnyType(StructType struct_) : kind(TypeKind::StructType), struct_(struct_) {}
    explicit inline AnyType(PolymorphicStruct polymorphic_struct) : kind(TypeKind::PolymorphicStruct), polymorphic_struct(polymorphic_struct) {}
    explicit inline AnyType(UnionType union_) : kind(TypeKind::UnionType), union_(union_) {}
    explicit inline AnyType(PolymorphicUnion polymorphic_union) : kind(TypeKind::PolymorphicUnion), polymorphic_union(polymorphic_union) {}
    explicit inline AnyType(UndeterminedStruct undetermined_struct) : kind(TypeKind::UndeterminedStruct), undetermined_struct(undetermined_struct) {}

    static inline AnyType create_polymorphic_function() {
        AnyType result;
        result.kind = TypeKind::PolymorphicFunction;

        return result;
    }

    static inline AnyType create_builtin_function() {
        AnyType result;
        result.kind = TypeKind::BuiltinFunction;

        return result;
    }

    static inline AnyType create_undetermined_integer() {
        AnyType result;
        result.kind = TypeKind::UndeterminedInteger;

        return result;
    }

    static inline AnyType create_undetermined_float() {
        AnyType result;
        result.kind = TypeKind::UndeterminedFloat;

        return result;
    }

    static inline AnyType create_type_type() {
        AnyType result;
        result.kind = TypeKind::Type;

        return result;
    }

    static inline AnyType create_void() {
        AnyType result;
        result.kind = TypeKind::Void;

        return result;
    }

    static inline AnyType create_boolean() {
        AnyType result;
        result.kind = TypeKind::Boolean;

        return result;
    }

    static inline AnyType create_file_module() {
        AnyType result;
        result.kind = TypeKind::FileModule;

        return result;
    }

    bool operator==(AnyType other);
    bool operator!=(AnyType other);
    String get_description();
    bool is_runtime_type();
    bool is_pointable_type();
    uint64_t get_alignment(ArchitectureSizes architecture_sizes);
    uint64_t get_size(ArchitectureSizes architecture_sizes);
};

struct StructTypeMember {
    String name;

    AnyType type;
};