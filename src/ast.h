#pragma once

struct FileRange {
    unsigned int first_line;
    unsigned int first_character;

    unsigned int last_line;
    unsigned int last_character;
};

struct Identifier {
    const char *text;

    FileRange range;
};

struct Expression;

struct FunctionParameter {
    Identifier name;

    bool is_polymorphic_determiner;

    Expression *type;

    Identifier polymorphic_determiner;
};

struct Expression {
    FileRange range;

    Expression(FileRange range) : range { range } {}

    virtual ~Expression() {}
};

struct NamedReference : Expression {
    Identifier name;

    NamedReference(
        FileRange range,
        Identifier name
    ) :
        Expression { range },
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
        Expression { range },
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
        Expression { range },
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
        Expression { range },
        value { value }
    {}
};

struct StringLiteral : Expression {
    Array<char> characters;

    StringLiteral(
        FileRange range,
        Array<char> characters
    ) :
        Expression { range },
        characters { characters }
    {}
};

struct ArrayLiteral : Expression {
    Array<Expression*> elements;

    ArrayLiteral(
        FileRange range,
        Array<Expression*> elements
    ) :
        Expression { range },
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
        Expression { range },
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
        Expression { range },
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
        Expression { range },
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
        Expression { range },
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
        Expression { range },
        expression { expression },
        type { type }
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
        Expression { range },
        expression { expression },
        index { index }
    {}
};

struct FunctionType : Expression {
    Array<FunctionParameter> parameters;

    Expression *return_type;

    FunctionType(
        FileRange range,
        Array<FunctionParameter> parameters,
        Expression *return_type
    ) :
        Expression { range },
        parameters { parameters },
        return_type { return_type }
    {}
};

struct Statement {
    FileRange range;

    Statement *parent;

    Statement(FileRange range) : range { range } {}

    virtual ~Statement() {}
};

struct FunctionDeclaration : Statement {
    Identifier name;

    Array<FunctionParameter> parameters;

    Expression *return_type;

    bool is_external;

    Array<Statement*> statements;

    Array<const char *> external_libraries;

    FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Expression *return_type,
        Array<Statement*> statements
    ) :
        Statement { range },
        name { name },
        parameters { parameters },
        return_type { return_type },
        is_external { false },
        statements { statements }
    {}

    FunctionDeclaration(
        FileRange range,
        Identifier name,
        Array<FunctionParameter> parameters,
        Expression *return_type,
        Array<const char *> external_libraries
    ) :
        Statement { range },
        name { name },
        parameters { parameters },
        return_type { return_type },
        is_external { true },
        external_libraries { external_libraries }
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
        Statement { range },
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
        Statement { range },
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
        Statement { range },
        expression { expression }
    {}
};

struct VariableDeclaration : Statement {
    Identifier name;

    Expression *type;
    Expression *initializer;

    VariableDeclaration(
        FileRange range,
        Identifier name,
        Expression *type,
        Expression *initializer
    ) :
        Statement { range },
        type { type },
        initializer { initializer}
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
        Statement { range },
        target { target },
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
        Statement { range },
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
        Statement { range },
        condition { condition },
        statements { statements }
    {}
};

struct ReturnStatement : Statement {
    Expression *value;

    ReturnStatement(
        FileRange range,
        Expression *value
    ) :
        Statement { range },
        value { value }
    {}
};

struct Import : Statement {
    const char *path;

    Import(
        FileRange range,
        const char *path
    ) :
        Statement { range },
        path { path }
    {}
};

struct UsingStatement : Statement {
    Expression *module;

    UsingStatement(
        FileRange range,
        Expression *module
    ) :
        Statement { range },
        module { module }
    {}
};