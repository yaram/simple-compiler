#pragma once

#include <stdint.h>
#include "array.h"

enum struct ExpressionType {
    NamedReference,
    IntegerLiteral,
    FunctionCall,
    Pointer
};

struct Expression {
    ExpressionType type;

    union {
        char *named_reference;

        int64_t integer_literal;

        struct {
            Expression *expression;

            Array<Expression> parameters;
        } function_call;

        Expression *pointer;
    };
};

void debug_print_expression(Expression expression);

enum struct StatementType {
    FunctionDefinition,
    ConstantDefinition,
    Expression
};

struct FunctionParameter {
    char *name;

    Expression type;
};

struct Statement {
    StatementType type;

    union {
        struct {
            const char *name;

            Array<FunctionParameter> parameters;

            Array<Statement> statements;
        } function_definition;

        struct {
            const char *name;

            Expression expression;
        } constant_definition;

        Expression expression;
    };
};

void debug_print_statement(Statement statement);