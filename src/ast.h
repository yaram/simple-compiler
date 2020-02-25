#pragma once

#include <stdint.h>
#include "array.h"

struct FileRange {
    const char *path;

    unsigned int start_line;
    unsigned int start_character;

    unsigned int end_line;
    unsigned int end_character;
};

struct Identifier {
    const char *text;

    FileRange range;
};

enum struct BinaryOperator {
    Addition,
    Subtraction,
    Multiplication,
    Division,
    Modulo,
    Equal,
    NotEqual,
    BitwiseAnd,
    BitwiseOr,
    BooleanAnd,
    BooleanOr
};

enum struct UnaryOperator {
    Pointer,
    BooleanInvert,
    Negation
};

enum struct ExpressionType {
    NamedReference,
    MemberReference,
    IndexReference,
    IntegerLiteral,
    StringLiteral,
    ArrayLiteral,
    StructLiteral,
    FunctionCall,
    BinaryOperation,
    UnaryOperation,
    Cast,
    ArrayType,
    FunctionType
};

struct StructLiteralMember;

struct FunctionParameter;

struct Expression {
    ExpressionType type;

    FileRange range;

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

        Array<StructLiteralMember> struct_literal;

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

        struct {
            Expression *expression;

            Expression *type;
        } cast;

        Expression *array_type;

        struct {
            Array<FunctionParameter> parameters;

            Expression *return_type;
        } function_type;
    };
};

struct StructLiteralMember {
    Identifier name;

    Expression value;
};

void print_expression(Expression expression);

enum struct StatementType {
    FunctionDeclaration,
    ConstantDefinition,
    StructDefinition,
    Expression,
    VariableDeclaration,
    Assignment,
    LoneIf,
    WhileLoop,
    Return,
    Import,
    Library
};

enum struct VariableDeclarationType {
    Uninitialized,
    TypeElided,
    FullySpecified
};

struct StructMember {
    Identifier name;

    Expression type;
};

struct FunctionParameter {
    Identifier name;

    bool is_polymorphic_determiner;

    union {
        Expression type;

        Identifier polymorphic_determiner;
    };
};

struct Statement {
    StatementType type;

    FileRange range;

    bool is_top_level;

    union {
        const char *file_name;

        Statement *parent;
    };

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

        struct {
            Identifier name;

            Array<StructMember> members;
        } struct_definition;

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

        struct {
            Expression condition;

            Array<Statement> statements;
        } while_loop;

        Expression _return;

        const char *import;

        const char *library;
    };
};

void print_statement(Statement statement);