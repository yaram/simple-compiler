#pragma once

#include <stdint.h>

enum struct ExpressionType {
    NamedReference
};

struct Expression {
    ExpressionType type;

    union {
        char *named_reference;
    };
};

void debug_print_expression(Expression expression);

enum struct StatementType {
    FunctionDeclaration,
    Expression
};

struct Statement {
    StatementType type;

    union {
        struct {
            const char *name;

            Statement *statements;
            size_t statement_count;
        } function_declaration;

        Expression expression;
    };
};

void debug_print_statement(Statement statement);