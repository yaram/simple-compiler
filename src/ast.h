#pragma once

#include <stdint.h>
#include "array.h"
#include "string.h"
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

    Expression* type;

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

    void print();
};

struct NamedReference : Expression {
    Identifier name;

    explicit inline NamedReference(
        FileRange range,
        Identifier name
    ) :
        Expression { ExpressionKind::NamedReference, range },
        name { name }
    {}
};

struct MemberReference : Expression {
    Expression* expression;

    Identifier name;

    explicit inline MemberReference(
        FileRange range,
        Expression* expression,
        Identifier name
    ) :
        Expression { ExpressionKind::MemberReference, range },
        expression { expression },
        name { name }
    {}
};

struct IndexReference : Expression {
    Expression* expression;

    Expression* index;

    explicit inline IndexReference(
        FileRange range,
        Expression* expression,
        Expression* index
    ) :
        Expression { ExpressionKind::IndexReference, range },
        expression { expression },
        index { index }
    {}
};

struct IntegerLiteral : Expression {
    uint64_t value;

    explicit inline IntegerLiteral(
        FileRange range,
        uint64_t value
    ) :
        Expression { ExpressionKind::IntegerLiteral, range },
        value { value }
    {}
};

struct FloatLiteral : Expression {
    double value;

    explicit inline FloatLiteral(
        FileRange range,
        double value
    ) :
        Expression { ExpressionKind::FloatLiteral, range },
        value { value }
    {}
};

struct StringLiteral : Expression {
    String characters;

    explicit inline StringLiteral(
        FileRange range,
        String characters
    ) :
        Expression { ExpressionKind::StringLiteral, range },
        characters { characters }
    {}
};

struct ArrayLiteral : Expression {
    Array<Expression*> elements;

    explicit inline ArrayLiteral(
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

        Expression* value;
    };

    Array<Member> members;

    explicit inline StructLiteral(
        FileRange range,
        Array<Member> members
    ) :
        Expression { ExpressionKind::StructLiteral, range },
        members { members }
    {}
};

struct FunctionCall : Expression {
    Expression* expression;

    Array<Expression*> parameters;

    explicit inline FunctionCall(
        FileRange range,
        Expression* expression,
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
        LeftShift,
        RightShift,
        BooleanAnd,
        BooleanOr
    };

    Operator binary_operator;

    Expression* left;

    Expression* right;

    explicit inline BinaryOperation(
        FileRange range,
        Operator binary_operator,
        Expression* left,
        Expression* right
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
        PointerDereference,
        BooleanInvert,
        Negation
    };

    Operator unary_operator;

    Expression* expression;

    explicit inline UnaryOperation(
        FileRange range,
        Operator unary_operator,
        Expression* expression
    ) :
        Expression { ExpressionKind::UnaryOperation, range },
        unary_operator { unary_operator },
        expression { expression }
    {}
};

struct Cast : Expression {
    Expression* expression;

    Expression* type;

    explicit inline Cast(
        FileRange range,
        Expression* expression,
        Expression* type
    ) :
        Expression { ExpressionKind::Cast, range },
        expression { expression },
        type { type }
    {}
};

struct Bake : Expression {
    FunctionCall* function_call;

    explicit inline Bake(
        FileRange range,
        FunctionCall* function_call
    ) :
        Expression { ExpressionKind::Bake, range },
        function_call { function_call }
    {}
};

struct ArrayType : Expression {
    Expression* expression;

    Expression* length;

    explicit inline ArrayType(
        FileRange range,
        Expression* expression,
        Expression* length
    ) :
        Expression { ExpressionKind::ArrayType, range },
        expression { expression },
        length { length }
    {}
};

struct FunctionType : Expression {
    Array<FunctionParameter> parameters;

    Array<Expression*> return_types;

    Array<Tag> tags;

    explicit inline FunctionType(
        FileRange range,
        Array<FunctionParameter> parameters,
        Array<Expression*> return_types,
        Array<Tag> tags
    ) :
        Expression { ExpressionKind::FunctionType, range },
        parameters { parameters },
        return_types { return_types },
        tags { tags }
    {}
};

enum struct StatementKind {
    FunctionDeclaration,
    ConstantDefinition,
    StructDefinition,
    UnionDefinition,
    EnumDefinition,
    ExpressionStatement,
    VariableDeclaration,
    MultiReturnVariableDeclaration,
    Assignment,
    BinaryOperationAssignment,
    MultiReturnAssignment,
    IfStatement,
    WhileLoop,
    ForLoop,
    ReturnStatement,
    BreakStatement,
    InlineAssembly,
    Import,
    UsingStatement,
    StaticIf
};

struct Statement {
    StatementKind kind;

    FileRange range;

    void print();
};

struct FunctionDeclaration : Statement {
    Identifier name;

    Array<FunctionParameter> parameters;

    Array<Expression*> return_types;

    Array<Tag> tags;

    bool has_body;
    Array<Statement*> statements;

    explicit inline FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Array<Expression*> return_types,
        Array<Tag> tags,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::FunctionDeclaration, range },
        name { name },
        parameters { parameters },
        return_types { return_types },
        tags { tags },
        has_body { true },
        statements { statements }
    {}

    explicit inline FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Array<Expression*> return_types,
        Array<Tag> tags
    ) :
        Statement { StatementKind::FunctionDeclaration, range },
        name { name },
        parameters { parameters },
        return_types { return_types },
        tags { tags },
        has_body { false }
    {}
};

struct ConstantDefinition : Statement {
    Identifier name;

    Expression* expression;

    explicit inline ConstantDefinition(
        FileRange range,
        Identifier name,
        Expression* expression
    ) :
        Statement { StatementKind::ConstantDefinition, range },
        name { name },
        expression { expression }
    {}
};

struct StructDefinition : Statement {
    struct Parameter {
        Identifier name;

        Expression* type;
    };

    struct Member {
        Identifier name;

        Expression* type;
    };

    Identifier name;

    Array<Parameter> parameters;

    Array<Member> members;

    explicit inline StructDefinition(
        FileRange range,
        Identifier name,
        Array<Parameter> parameters,
        Array<Member> members
    ) :
        Statement { StatementKind::StructDefinition, range },
        name { name },
        parameters { parameters },
        members { members }
    {}
};

struct UnionDefinition : Statement {
    struct Parameter {
        Identifier name;

        Expression* type;
    };

    struct Member {
        Identifier name;

        Expression* type;
    };

    Identifier name;

    Array<Parameter> parameters;

    Array<Member> members;

    explicit inline UnionDefinition(
        FileRange range,
        Identifier name,
        Array<Parameter> parameters,
        Array<Member> members
    ) :
        Statement { StatementKind::UnionDefinition, range },
        name { name },
        parameters { parameters },
        members { members }
    {}
};

struct EnumDefinition : Statement {
    struct Variant {
        Identifier name;

        Expression* value;
    };

    Identifier name;

    Expression* backing_type;

    Array<Variant> variants;

    explicit inline EnumDefinition(
        FileRange range,
        Identifier name,
        Expression* backing_type,
        Array<Variant> variants
    ) :
        Statement { StatementKind::EnumDefinition, range },
        name { name },
        backing_type { backing_type },
        variants { variants }
    {}
};

struct ExpressionStatement : Statement {
    Expression* expression;

    explicit inline ExpressionStatement(
        FileRange range,
        Expression* expression
    ) :
        Statement { StatementKind::ExpressionStatement, range },
        expression { expression }
    {}
};

struct VariableDeclaration : Statement {
    Identifier name;

    Expression* type;
    Expression* initializer;

    Array<Tag> tags;

    explicit inline VariableDeclaration(
        FileRange range,
        Identifier name,
        Expression* type,
        Expression* initializer,
        Array<Tag> tags
    ) :
        Statement { StatementKind::VariableDeclaration, range },
        name { name },
        type { type },
        initializer { initializer },
        tags { tags }
    {}
};

struct MultiReturnVariableDeclaration : Statement {
    Array<Identifier> names;

    Expression* initializer;

    explicit inline MultiReturnVariableDeclaration(
        FileRange range,
        Array<Identifier> names,
        Expression* initializer
    ) :
        Statement { StatementKind::MultiReturnVariableDeclaration, range },
        names { names },
        initializer { initializer }
    {}
};

struct Assignment : Statement {
    Expression* target;

    Expression* value;

    explicit inline Assignment(
        FileRange range,
        Expression* target,
        Expression* value
    ) :
        Statement { StatementKind::Assignment, range },
        target { target },
        value { value }
    {}
};

struct MultiReturnAssignment : Statement {
    Array<Expression*> targets;

    Expression* value;

    explicit inline MultiReturnAssignment(
        FileRange range,
        Array<Expression*> targets,
        Expression* value
    ) :
        Statement { StatementKind::MultiReturnAssignment, range },
        targets { targets },
        value { value }
    {}
};

struct BinaryOperationAssignment : Statement {
    Expression* target;

    BinaryOperation::Operator binary_operator;

    Expression* value;

    explicit inline BinaryOperationAssignment(
        FileRange range,
        Expression* target,
        BinaryOperation::Operator binary_operator,
        Expression* value
    ) :
        Statement { StatementKind::BinaryOperationAssignment, range },
        target { target },
        binary_operator { binary_operator },
        value { value }
    {}
};

struct IfStatement : Statement {
    struct ElseIf {
        Expression* condition;

        Array<Statement*> statements;
    };

    Expression* condition;

    Array<Statement*> statements;

    Array<ElseIf> else_ifs;

    Array<Statement*> else_statements;

    explicit inline IfStatement(
        FileRange range,
        Expression* condition,
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
    Expression* condition;

    Array<Statement*> statements;

    explicit inline WhileLoop(
        FileRange range,
        Expression* condition,
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

    Expression* from;
    Expression* to;

    Array<Statement*> statements;

    explicit inline ForLoop(
        FileRange range,
        Expression* from,
        Expression* to,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::ForLoop, range },
        has_index_name { false },
        from { from },
        to { to },
        statements { statements }
    {}

    explicit inline ForLoop(
        FileRange range,
        Identifier index_name,
        Expression* from,
        Expression* to,
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
    Array<Expression*> values;

    explicit inline ReturnStatement(
        FileRange range,
        Array<Expression*> values
    ) :
        Statement { StatementKind::ReturnStatement, range },
        values { values }
    {}
};

struct BreakStatement : Statement {
    explicit inline BreakStatement(
        FileRange range
    ) :
        Statement { StatementKind::BreakStatement, range }
    {}
};

struct InlineAssembly : Statement {
    struct Binding {
        String constraint;

        Expression* value;
    };

    String assembly;

    Array<Binding> bindings;

    explicit inline InlineAssembly(
        FileRange range,
        String assembly,
        Array<Binding> bindings
    ) :
        Statement { StatementKind::InlineAssembly, range },
        assembly { assembly },
        bindings { bindings }
    {}
};

struct Import : Statement {
    String path;
    String absolute_path;
    String name;

    explicit inline Import(
        FileRange range,
        String path,
        String absolute_path,
        String name
    ) :
        Statement { StatementKind::Import, range },
        path { path },
        absolute_path { absolute_path },
        name { name }
    {}
};

struct UsingStatement : Statement {
    bool export_;

    Expression* value;

    explicit inline UsingStatement(
        FileRange range,
        bool export_,
        Expression* value
    ) :
        Statement { StatementKind::UsingStatement, range },
        export_ { export_ },
        value { value }
    {}
};

struct StaticIf : Statement {
    Expression* condition;
    Array<Statement*> statements;

    explicit inline StaticIf(
        FileRange range,
        Expression* condition,
        Array<Statement*> statements
    ) :
        Statement { StatementKind::StaticIf, range },
        condition { condition },
        statements { statements }
    {}
};