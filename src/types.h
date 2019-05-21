#pragma once

enum struct TypeCategory {
    Function,
    Integer,
    Type
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
            bool is_signed;

            IntegerSize size;
        } integer;
    };
};

bool types_equal(Type a, Type b);