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
    FileModule
};

enum struct IntegerType {
    Undetermined,
    Unsigned8,
    Unsigned16,
    Unsigned32,
    Unsigned64,
    Signed8,
    Signed16,
    Signed32,
    Signed64
};

struct Type {
    TypeCategory category;

    union {
        struct {
            Array<Type> parameters;

            Type *return_type;
        } function;

        IntegerType integer;

        Type *pointer;

        Type *array;

        struct {
            size_t length;

            Type *type;
        } static_array;
    };
};

bool types_equal(Type a, Type b);