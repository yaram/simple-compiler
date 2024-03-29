#pragma once

#include <stdint.h>
#include "array.h"
#include "util.h"

struct Identifier {
    String text;

    FileRange range;
};

struct Expression;

struct FunctionParameter {
    Identifier name;

    bool is_constant;

    bool is_polymorphic_determiner;

    Expression *type;

    Identifier polymorphic_determiner;
};

struct Tag {
    Identifier name;

    Array<Expression*> parameters;

    FileRange range;
};

enum struct ExpressionKind {
    NamedReference,
    MemberReference,
    IndexReference,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    ArrayLiteral,
    StructLiteral,
    FunctionCall,
    BinaryOperation,
    UnaryOperation,
    Cast,
    Bake,
    ArrayType,
    FunctionType
};

struct Expression {
    ExpressionKind kind;

    FileRange range;
};

struct NamedReference : Expression {
    Identifier name;

    NamedReference(
        FileRange range,
        Identifier name
    ) :
        Expression { ExpressionKind::NamedReference, range },
        name { name }
    {}
};

struct MemberReference : Expression {
    Expression *expression;

    Identifier name;

    MemberReference(
        FileRange range,
        Expression *expression,
        Identifier name
    ) :
        Expression { ExpressionKind::MemberReference, range },
        expression { expression },
        name { name }
    {}
};

struct IndexReference : Expression {
    Expression *expression;

    Expression *index;

    IndexReference(
        FileRange range,
        Expression *expression,
        Expression *index
    ) :
        Expression { ExpressionKind::IndexReference, range },
        expression { expression },
        index { index }
    {}
};

struct IntegerLiteral : Expression {
    uint64_t value;

    IntegerLiteral(
        FileRange range,
        uint64_t value
    ) :
        Expression { ExpressionKind::IntegerLiteral, range },
        value { value }
    {}
};

struct FloatLiteral : Expression {
    double value;

    FloatLiteral(
        FileRange range,
        double value
    ) :
        Expression { ExpressionKind::FloatLiteral, range },
        value { value }
    {}
};

struct StringLiteral : Expression {
    Array<char> characters;

    StringLiteral(
        FileRange range,
        Array<char> characters
    ) :
        Expression { ExpressionKind::StringLiteral, range },
        characters { characters }
    {}
};

struct ArrayLiteral : Expression {
    Array<Expression*> elements;

    ArrayLiteral(
        FileRange range,
        Array<Expression*> elements
    ) :
        Expression { ExpressionKind::ArrayLiteral, range },
        elements { elements }
    {}
};

struct StructLiteral : Expression {
    struct Member {
        Identifier name;

        Expression *value;
    };

    Array<Member> members;

    StructLiteral(
        FileRange range,
        Array<Member> members
    ) :
        Expression { ExpressionKind::StructLiteral, range },
        members { members }
    {}
};

struct FunctionCall : Expression {
    Expression *expression;

    Array<Expression*> parameters;

    FunctionCall(
        FileRange range,
        Expression *expression,
        Array<Expression*> parameters
    ) :
        Expression { ExpressionKind::FunctionCall, range },
        expression { expression },
        parameters { parameters }
    {}
};

struct BinaryOperation : Expression {
    enum struct Operator {
        Addition,
        Subtraction,
        Multiplication,
        Division,
        Modulo,
        Equal,
        NotEqual,
        LessThan,
        GreaterThan,
        BitwiseAnd,
        BitwiseOr,
        BooleanAnd,
        BooleanOr
    };

    Operator binary_operator;

    Expression *left;

    Expression *right;

    BinaryOperation(
        FileRange range,
        Operator binary_operator,
        Expression *left,
        Expression *right
    ) :
        Expression { ExpressionKind::BinaryOperation, range },
        binary_operator { binary_operator },
        left { left },
        right { right }
    {}
};

struct UnaryOperation : Expression {
    enum struct Operator {
        Pointer,
        BooleanInvert,
        Negation
    };

    Operator unary_operator;

    Expression *expression;

    UnaryOperation(
        FileRange range,
        Operator unary_operator,
        Expression *expression
    ) :
        Expression { ExpressionKind::UnaryOperation, range },
        unary_operator { unary_operator },
        expression { expression }
    {}
};

struct Cast : Expression {
    Expression *expression;

    Expression *type;

    Cast(
        FileRange range,
        Expression *expression,
        Expression *type
    ) :
        Expression { ExpressionKind::Cast, range },
        expression { expression },
        type { type }
    {}
};

struct Bake : Expression {
    FunctionCall *function_call;

    Bake(
        FileRange range,
        FunctionCall *function_call
    ) :
        Expression { ExpressionKind::Bake, range },
        function_call { function_call }
    {}
};

struct ArrayType : Expression {
    Expression *expression;

    Expression *index;

    ArrayType(
        FileRange range,
        Expression *expression,
        Expression *index
    ) :
        Expression { ExpressionKind::ArrayType, range },
        expression { expression },
        index { index }
    {}
};

struct FunctionType : Expression {
    Array<FunctionParameter> parameters;

    Expression *return_type;

    Array<Tag> tags;

    FunctionType(
        FileRange range,
        Array<FunctionParameter> parameters,
        Expression *return_type,
        Array<Tag> tags
    ) :
        Expression { ExpressionKind::FunctionType, range },
        parameters { parameters },
        return_type { return_type },
        tags { tags }
    {}
};

void print_expression(Expression *expression);

enum struct StatementKind {
    FunctionDeclaration,
    ConstantDefinition,
    StructDefinition,
    ExpressionStatement,
    VariableDeclaration,
    Assignment,
    BinaryOperationAssignment,
    IfStatement,
    WhileLoop,
    ForLoop,
    ReturnStatement,
    BreakStatement,
    Import,
    UsingStatement,
    StaticIf
};

struct Statement {
    StatementKind kind;

    FileRange range;
};

struct FunctionDeclaration : Statement {
    Identifier name;

    Array<FunctionParameter> parameters;

    Expression *return_type;

    Array<Tag> tags;

    bool has_body;
    Array<Statement*> statements;

    FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Expression *return_type,
        Array<Tag> tags,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::FunctionDeclaration, range },
        name { name },
        parameters { parameters },
        return_type { return_type },
        tags { tags },
        has_body { true },
        statements { statements }
    {}

    FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Expression *return_type,
        Array<Tag> tags
    ) :
        Statement { StatementKind::FunctionDeclaration, range },
        name { name },
        parameters { parameters },
        return_type { return_type },
        tags { tags },
        has_body { false }
    {}
};

struct ConstantDefinition : Statement {
    Identifier name;

    Expression *expression;

    ConstantDefinition(
        FileRange range,
        Identifier name,
        Expression *expression
    ) :
        Statement { StatementKind::ConstantDefinition, range },
        name { name },
        expression { expression }
    {}
};

struct StructDefinition : Statement {
    struct Parameter {
        Identifier name;

        Expression *type;
    };

    struct Member {
        Identifier name;

        Expression *type;
    };

    Identifier name;

    bool is_union;

    Array<Parameter> parameters;

    Array<Member> members;

    StructDefinition(
        FileRange range,
        Identifier name,
        bool is_union,
        Array<Parameter> parameters,
        Array<Member> members
    ) :
        Statement { StatementKind::StructDefinition, range },
        name { name },
        is_union { is_union },
        parameters { parameters },
        members { members }
    {}
};

struct ExpressionStatement : Statement {
    Expression *expression;

    ExpressionStatement(
        FileRange range,
        Expression *expression
    ) :
        Statement { StatementKind::ExpressionStatement, range },
        expression { expression }
    {}
};

struct VariableDeclaration : Statement {
    Identifier name;

    Expression *type;
    Expression *initializer;

    Array<Tag> tags;

    VariableDeclaration(
        FileRange range,
        Identifier name,
        Expression *type,
        Expression *initializer,
        Array<Tag> tags
    ) :
        Statement { StatementKind::VariableDeclaration, range },
        name { name },
        type { type },
        initializer { initializer },
        tags { tags }
    {}
};

struct Assignment : Statement {
    Expression *target;

    Expression *value;

    Assignment(
        FileRange range,
        Expression *target,
        Expression *value
    ) :
        Statement { StatementKind::Assignment, range },
        target { target },
        value { value }
    {}
};

struct BinaryOperationAssignment : Statement {
    Expression *target;

    BinaryOperation::Operator binary_operator;

    Expression *value;

    BinaryOperationAssignment(
        FileRange range,
        Expression *target,
        BinaryOperation::Operator binary_operator,
        Expression *value
    ) :
        Statement { StatementKind::BinaryOperationAssignment, range },
        target { target },
        binary_operator { binary_operator },
        value { value }
    {}
};

struct IfStatement : Statement {
    struct ElseIf {
        Expression *condition;

        Array<Statement*> statements;
    };

    Expression *condition;

    Array<Statement*> statements;

    Array<ElseIf> else_ifs;

    Array<Statement*> else_statements;

    IfStatement(
        FileRange range,
        Expression *condition,
        Array<Statement*> statements,
        Array<ElseIf> else_ifs,
        Array<Statement*> else_statements
    ) :
        Statement { StatementKind::IfStatement, range },
        condition { condition },
        statements { statements },
        else_ifs { else_ifs },
        else_statements { else_statements }
    {}
};

struct WhileLoop : Statement {
    Expression *condition;

    Array<Statement*> statements;

    WhileLoop(
        FileRange range,
        Expression *condition,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::WhileLoop, range },
        condition { condition },
        statements { statements }
    {}
};

struct ForLoop : Statement {
    bool has_index_name;
    Identifier index_name;

    Expression *from;
    Expression *to;

    Array<Statement*> statements;

    ForLoop(
        FileRange range,
        Expression *from,
        Expression *to,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::ForLoop, range },
        has_index_name { false },
        from { from },
        to { to },
        statements { statements }
    {}

    ForLoop(
        FileRange range,
        Identifier index_name,
        Expression *from,
        Expression *to,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::ForLoop, range },
        has_index_name { true },
        index_name { index_name },
        from { from },
        to { to },
        statements { statements }
    {}
};

struct ReturnStatement : Statement {
    Expression *value;

    ReturnStatement(
        FileRange range,
        Expression *value
    ) :
        Statement { StatementKind::ReturnStatement, range },
        value { value }
    {}
};

struct BreakStatement : Statement {
    BreakStatement(
        FileRange range
    ) :
        Statement { StatementKind::BreakStatement, range }
    {}
};

struct Import : Statement {
    const char *path;
    const char *absolute_path;
    String name;

    Import(
        FileRange range,
        const char *path,
        const char *absolute_path,
        String name
    ) :
        Statement { StatementKind::Import, range },
        path { path },
        absolute_path { absolute_path },
        name { name }
    {}
};

struct UsingStatement : Statement {
    Expression *module;

    UsingStatement(
        FileRange range,
        Expression *module
    ) :
        Statement { StatementKind::UsingStatement, range },
        module { module }
    {}
};

struct StaticIf : Statement {
    Expression *condition;
    Array<Statement*> statements;

    StaticIf(
        FileRange range,
        Expression *condition,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::StaticIf, range },
        statements { statements },
        condition { condition }
    {}
};

void print_statement(Statement *statement);