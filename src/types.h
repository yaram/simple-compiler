#pragma once

#include "array.h"
#include "register_size.h"

enum struct TypeCategory {
    Function,
    Integer,
    Boolean,
    Type,
    Void,
    Pointer,
    Array,
    StaticArray,
    Struct,
    FileModule
};

struct StructTypeMember;

struct Type {
    TypeCategory category;

    union {
        struct {
            bool is_polymorphic;

            size_t parameter_count;

            Type *parameters;

            Type *return_type;
        } function;

        struct {
            RegisterSize size;

            bool is_signed;

            bool is_undetermined;
        } integer;

        Type *pointer;

        Type *array;

        struct {
            size_t length;

            Type *type;
        } static_array;

        struct {
            bool is_undetermined;

            union {
                Array<StructTypeMember> members;

                const char *name;
            };
        } _struct;
    };
};

struct StructTypeMember {
    const char *name;

    Type type;
};

bool types_equal(Type a, Type b);
const char *type_description(Type type);
const char *determined_integer_type_description(RegisterSize size, bool is_signed);
bool is_type_undetermined(Type type);