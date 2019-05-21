#pragma once

#include "array.h"

enum struct TypeCategory {
    Function,
    Integer,
    Type,
    Void,
    Pointer
};

enum struct IntegerSize {
    Bit8,
    Bit16,
    Bit32,
    Bit64,
};

struct Type {
    TypeCategory category;

    union {
        struct {
            Array<Type> parameters;

            Type *return_type;
        } function;

        struct {
            bool is_signed;

            IntegerSize size;
        } integer;

        Type *pointer;
    };
};

bool types_equal(Type a, Type b);