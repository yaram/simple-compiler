#pragma once

#include "array.h"

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

enum struct IntegerSize {
    Size8,
    Size16,
    Size32,
    Size64
};

struct Type {
    TypeCategory category;

    union {
        struct {
            bool is_polymorphic;

            Array<Type> parameters;

            Type *return_type;
        } function;

        struct {
            IntegerSize size;

            bool is_signed;
        } integer;

        Type *pointer;

        Type *array;

        struct {
            size_t length;

            Type *type;
        } static_array;

        const char *_struct;
    };
};

bool types_equal(Type a, Type b);
const char *type_description(Type type);