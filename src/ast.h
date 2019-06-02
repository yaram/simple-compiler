#pragma once

#include <stdint.h>
#include "array.h"

enum struct ExpressionType {
    NamedReference,
    MemberReference,
    IndexReference,
    IntegerLiteral,
    StringLiteral,
    FunctionCall,
    Pointer,
    ArrayType
};

struct Expression {
    ExpressionType type;

    const char *source_file_path;

    unsigned int line;
    unsigned int character;

    union {
        char *named_reference;

        struct {
            Expression *expression;

            const char *name;
        } member_reference;

        struct {
            Expression *expression;

            Expression *index;
        } index_reference;

        int64_t integer_literal;

        Array<char> string_literal;

        struct {
            Expression *expression;

            Array<Expression> parameters;
        } function_call;

        Expression *pointer;

        Expression *array_type;
    };
};

void debug_print_expression(Expression expression);

enum struct StatementType {
    FunctionDeclaration,
    ConstantDefinition,
    Expression,
    VariableDeclaration,
    Assignment
};

struct FunctionParameter {
    char *name;

    Expression type;
};

struct Statement {
    StatementType type;

    const char *source_file_path;

    unsigned int line;
    unsigned int character;

    union {
        struct {
            const char *name;

            Array<FunctionParameter> parameters;

            bool has_return_type;
            Expression return_type;

            bool is_external;

            Array<Statement> statements;
        } function_declaration;

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

        struct {
            Expression target;

            Expression value;
        } assignment;
    };
};

void debug_print_statement(Statement statement);