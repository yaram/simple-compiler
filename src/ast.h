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
    Expression,
    VariableDeclaration
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

            bool has_return_type;
            Expression return_type;

            Array<Statement> statements;
        } function_definition;

        struct {
            const char *name;

            Expression expression;
        } constant_definition;

        Expression expression;

        struct {
            const char *name;

            bool has_type;
            Expression type;

            bool has_initializer;
            Expression initializer;
        } variable_declaration;
    };
};

void debug_print_statement(Statement statement);