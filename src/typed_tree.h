#pragma once

#include "array.h"
#include "types.h"

struct TypedExpression {
    FileRange range;

    Array<TypedExpression> children;

    AnyType type;
};

struct TypedStatement {
    FileRange range;

    Array<TypedStatement> children;
    Array<TypedExpression> expressions;
};