#pragma once

#include <stdint.h>
#include "array.h"

struct FilePosition {
    const char *path;

    unsigned int line;
    unsigned int character;
};

struct Identifier {
    const char *text;

    FilePosition position;
};

enum struct BinaryOperator {
    Addition,
    Subtraction,
    Multiplication,
    Division,
    Modulo,
    Equal,
    NotEqual
};

enum struct UnaryOperator {
    Pointer,
    BooleanInvert
};

enum struct ExpressionType {
    NamedReference,
    MemberReference,
    IndexReference,
    IntegerLiteral,
    StringLiteral,
    ArrayLiteral,
    FunctionCall,
    BinaryOperation,
    UnaryOperation,
    ArrayType
};

struct Expression {
    ExpressionType type;

    FilePosition position;

    union {
        Identifier named_reference;

        struct {
            Expression *expression;

            Identifier name;
        } member_reference;

        struct {
            Expression *expression;

            Expression *index;
        } index_reference;

        int64_t integer_literal;

        Array<char> string_literal;

        Array<Expression> array_literal;

        struct {
            Expression *expression;

            Array<Expression> parameters;
        } function_call;

        struct {
            BinaryOperator binary_operator;

            Expression *left;

            Expression *right;
        } binary_operation;

        struct {
            UnaryOperator unary_operator;

            Expression *expression;
        } unary_operation;

        Expression *array_type;
    };
};

void debug_print_expression(Expression expression);

enum struct StatementType {
    FunctionDeclaration,
    ConstantDefinition,
    Expression,
    VariableDeclaration,
    Assignment,
    LoneIf,
    Return,
    Import,
    Library
};

struct FunctionParameter {
    Identifier name;

    Expression type;
};

enum struct VariableDeclarationType {
    Uninitialized,
    TypeElided,
    FullySpecified
};

struct Statement {
    StatementType type;

    FilePosition position;

    union {
        struct {
            Identifier name;

            Array<FunctionParameter> parameters;

            bool has_return_type;
            Expression return_type;

            bool is_external;

            Array<Statement> statements;
        } function_declaration;

        struct {
            Identifier name;

            Expression expression;
        } constant_definition;

        Expression expression;

        struct {
            VariableDeclarationType type;

            Identifier name;

            union {
                Expression uninitialized;

                Expression type_elided;

                struct {
                    Expression type;

                    Expression initializer;
                } fully_specified;
            };
        } variable_declaration;

        struct {
            Expression target;

            Expression value;
        } assignment;

        struct {
            Expression condition;

            Array<Statement> statements;
        } lone_if;

        Expression _return;

        const char *import;

        const char *library;
    };
};

void debug_print_statement(Statement statement);