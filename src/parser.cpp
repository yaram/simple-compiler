#include "parser.h"
#include <stdio.h>
#include <stdarg.h>
#include "list.h"
#include "util.h"

struct Context {
    const char *path;

    Array<Token> tokens;

    size_t next_token_index;
};

static void error(Context context, const char *format, ...) {
    auto token = context.tokens[context.next_token_index];

    va_list arguments;
    va_start(arguments, format);

    fprintf(stderr, "Error: %s(%d,%d): ", context.path, token.line, token.first_character);
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");

    auto file = fopen(context.path, "rb");

    if(file != nullptr) {
        unsigned int current_line = 1;

        while(current_line != token.line) {
            auto character = fgetc(file);

            switch(character) {
                case '\r': {
                    auto character = fgetc(file);

                    if(character == '\n') {
                        current_line += 1;
                    } else {
                        ungetc(character, file);

                        current_line += 1;
                    }
                } break;

                case '\n': {
                    current_line += 1;
                } break;

                case EOF: {
                    fclose(file);

                    va_end(arguments);

                    return;
                } break;
            }
        }

        unsigned int skipped_spaces = 0;
        auto done_skipping_spaces = false;

        auto done = false;
        while(!done) {
            auto character = fgetc(file);

            switch(character) {
                case '\r':
                case '\n': {
                    done = true;
                } break;

                case ' ': {
                    if(done_skipping_spaces) {
                        skipped_spaces += 1;
                    } else {
                        fprintf(stderr, "%c", character);
                    }
                } break;

                case EOF: {
                    fclose(file);

                    va_end(arguments);

                    return;
                } break;

                default: {
                    fprintf(stderr, "%c", character);

                    done_skipping_spaces = true;
                } break;
            }
        }

        fprintf(stderr, "\n");

        for(unsigned int i = 1; i < token.first_character - skipped_spaces; i += 1) {
            fprintf(stderr, " ");
        }

        if(token.last_character - token.first_character == 0) {
            fprintf(stderr, "^");
        } else {
            for(unsigned int i = token.first_character; i <= token.last_character; i += 1) {
                fprintf(stderr, "-");
            }
        }

        fprintf(stderr, "\n");

        fclose(file);
    }

    va_end(arguments);
}

static FileRange token_range(Context context, Token token) {
    return {
        context.path,
        token.line,
        token.first_character,
        token.line,
        token.last_character
    };
}

static FileRange span_range(FileRange first, FileRange last) {
    return {
        first.path,
        first.start_line,
        first.start_character,
        last.end_character,
        last.end_line
    };
}

static Result<Token> next_token(Context context) {
    if(context.next_token_index < context.tokens.count) {

    } else {
        fprintf(stderr, "Error: %s: Unexpected end of file\n", context.path);

        return { false };
    }
}

static bool expect_basic_token(Context *context, TokenType type) {
    expect(token, next_token(*context));

    if(token.type != type) {
        Token expected_token;
        expected_token.type = type;

        error(*context, "Expected '%s', got '%s'", get_token_text(expected_token), get_token_text(token));

        return false;
    }

    context->next_token_index += 1;

    return true;
}

static Result<Array<char>> expect_string(Context *context) {
    expect(token, next_token(*context));

    if(token.type != TokenType::String) {
        error(*context, "Expected a string, got '%s'", get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

    return {
        true,
        token.string
    };
}

static Result<Identifier> expect_identifier(Context *context) {
    expect(token, next_token(*context));

    if(token.type != TokenType::Identifier) {
        error(*context, "Expected an identifier, got '%s'", get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

    return {
        true,
        {
            token.identifier,
            token_range(*context, token)
        }
    };
}

static Result<uint64_t> expect_integer(Context *context) {
    expect(token, next_token(*context));

    if(token.type != TokenType::Integer) {
        error(*context, "Expected an integer, got '%s'", get_token_text(token));

        return { false };
    }

    context->next_token_index += 1;

    return {
        true,
        token.integer
    };
}

static Identifier identifier_from_token(Context context, Token token) {
    return {
        token.identifier,
        token_range(context, token)
    };
}

static Expression named_reference_from_identifier(Identifier identifier) {
    Expression expression;
    expression.type = ExpressionType::NamedReference;
    expression.range = identifier.range;
    expression.named_reference = identifier;

    return expression;
}

enum struct OperationType {
    FunctionCall,
    MemberReference,
    IndexReference,
    ArrayType,

    Pointer,
    BooleanInvert,
    Negation,

    Cast,

    Multiplication,
    Division,
    Modulo,

    Addition,
    Subtraction,

    Equal,
    NotEqual,

    BitwiseAnd,

    BitwiseOr,

    BooleanAnd,

    BooleanOr
};

unsigned int operation_precedences[] = {
    1, 1, 1, 1,
    2, 2, 2,
    3,
    4, 4, 4,
    5, 5,
    8, 8,
    9,
    11,
    12,
    13
};

struct Operation {
    OperationType type;

    FileRange range;

    union {
        Identifier member_reference;

        Expression index_reference;

        Array<Expression> function_call;
    };
};

static void apply_operation(List<Expression> *expression_stack, Operation operation) {
    Expression expression;

    switch(operation.type) {
        case OperationType::Addition:
        case OperationType::Subtraction:
        case OperationType::Multiplication:
        case OperationType::Division:
        case OperationType::Modulo:
        case OperationType::Equal:
        case OperationType::NotEqual:
        case OperationType::BitwiseAnd:
        case OperationType::BitwiseOr:
        case OperationType::BooleanAnd:
        case OperationType::BooleanOr: {
            auto right = take_last(expression_stack);
            auto left = take_last(expression_stack);

            expression.type = ExpressionType::BinaryOperation;
            expression.range = span_range(left.range, right.range);

            expression.binary_operation.left = heapify(left);
            expression.binary_operation.right = heapify(right);

            switch(operation.type) {
                case OperationType::Addition: {
                    expression.binary_operation.binary_operator = BinaryOperator::Addition;
                } break;

                case OperationType::Subtraction: {
                    expression.binary_operation.binary_operator = BinaryOperator::Subtraction;
                } break;

                case OperationType::Multiplication: {
                    expression.binary_operation.binary_operator = BinaryOperator::Multiplication;
                } break;

                case OperationType::Division: {
                    expression.binary_operation.binary_operator = BinaryOperator::Division;
                } break;

                case OperationType::Modulo: {
                    expression.binary_operation.binary_operator = BinaryOperator::Modulo;
                } break;

                case OperationType::Equal: {
                    expression.binary_operation.binary_operator = BinaryOperator::Equal;
                } break;

                case OperationType::NotEqual: {
                    expression.binary_operation.binary_operator = BinaryOperator::NotEqual;
                } break;

                case OperationType::BitwiseAnd: {
                    expression.binary_operation.binary_operator = BinaryOperator::BitwiseAnd;
                } break;

                case OperationType::BitwiseOr: {
                    expression.binary_operation.binary_operator = BinaryOperator::BitwiseOr;
                } break;

                case OperationType::BooleanAnd: {
                    expression.binary_operation.binary_operator = BinaryOperator::BooleanAnd;
                } break;

                case OperationType::BooleanOr: {
                    expression.binary_operation.binary_operator = BinaryOperator::BooleanOr;
                } break;

                default: {
                    abort();
                } break;
            }
        } break;

        case OperationType::MemberReference: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::MemberReference;
            expression.range = span_range(sub_expression.range, operation.member_reference.range);

            expression.member_reference = {
                heapify(sub_expression),
                operation.member_reference
            };
        } break;

        case OperationType::IndexReference: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::IndexReference;
            expression.range = span_range(sub_expression.range, operation.range);

            expression.index_reference = {
                heapify(sub_expression),
                heapify(operation.index_reference)
            };
        } break;

        case OperationType::FunctionCall: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::FunctionCall;
            expression.range = span_range(sub_expression.range, operation.range);

            expression.function_call = {
                heapify(sub_expression),
                operation.function_call
            };
        } break;

        case OperationType::Pointer: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::UnaryOperation;
            expression.range = span_range(operation.range, sub_expression.range);

            expression.unary_operation = {
                UnaryOperator::Pointer,
                heapify(sub_expression)
            };
        } break;

        case OperationType::BooleanInvert: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::UnaryOperation;
            expression.range = span_range(operation.range, sub_expression.range);

            expression.unary_operation = {
                UnaryOperator::BooleanInvert,
                heapify(sub_expression)
            };
        } break;

        case OperationType::Negation: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::UnaryOperation;
            expression.range = span_range(operation.range, sub_expression.range);

            expression.unary_operation = {
                UnaryOperator::Negation,
                heapify(sub_expression)
            };
        } break;

        case OperationType::Cast: {
            auto type = take_last(expression_stack);
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::Cast;
            expression.range = span_range(sub_expression.range, type.range);

            expression.cast = {
                heapify(sub_expression),
                heapify(type)
            };
        } break;

        case OperationType::ArrayType: {
            auto sub_expression = take_last(expression_stack);

            expression.type = ExpressionType::ArrayType;
            expression.range = span_range(sub_expression.range, operation.range);

            expression.array_type = heapify(sub_expression);
        } break;

        default: {
            abort();
        } break;
    }

    append(expression_stack, expression);
}

static Result<Expression> parse_expression(Context *context);

static Result<FunctionParameter> parse_function_parameter_second_half(Context *context, Identifier identifier) {
    if(!expect_basic_token(context, TokenType::Colon)) {
        return { false };
    }

    FunctionParameter parameter;
    parameter.name = identifier;

    expect(token, next_token(*context));

    switch(token.type) {
        case TokenType::Dollar: {
            context->next_token_index += 1;

            expect(name, expect_identifier(context));

            parameter.is_polymorphic_determiner = true;
            parameter.polymorphic_determiner = name;
        } break;

        default: {
            expect(expression, parse_expression(context));

            parameter.is_polymorphic_determiner = false;
            parameter.type = expression;
        } break;
    }

    return {
        true,
        parameter
    };
}

static Result<Expression> parse_right_expressions(Context *context, List<Operation> *operation_stack, List<Expression> *expression_stack, bool start_with_left_recursive) {
    auto expect_left_recursive = start_with_left_recursive;

    while(true) {
        if(!expect_left_recursive) {
            expect(token, next_token(*context));

            // Parse non-left-recursive expressions first
            switch(token.type) {
                case TokenType::Identifier: {
                    context->next_token_index += 1;

                    auto range = token_range(*context, token);

                    auto identifier = identifier_from_token(*context, token);

                    auto expression = named_reference_from_identifier(identifier);

                    append(expression_stack, expression);
                } break;

                case TokenType::Integer: {
                    context->next_token_index += 1;

                    Expression expression;
                    expression.type = ExpressionType::IntegerLiteral;
                    expression.range = token_range(*context, token);
                    expression.integer_literal = (int64_t)token.integer;

                    append(expression_stack, expression);
                } break;

                case TokenType::Asterisk: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::Pointer;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::Bang: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::BooleanInvert;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::Dash: {
                    context->next_token_index += 1;

                    Operation operation;
                    operation.type = OperationType::Negation;
                    operation.range = token_range(*context, token);

                    append(operation_stack, operation);

                    continue;
                } break;

                case TokenType::String: {
                    context->next_token_index += 1;

                    Expression expression;
                    expression.type = ExpressionType::StringLiteral;
                    expression.range = token_range(*context, token);
                    expression.string_literal = token.string;

                    append(expression_stack, expression);
                } break;

                case TokenType::OpenRoundBracket: {
                    context->next_token_index += 1;

                    auto first_range = token_range(*context, token);

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::CloseRoundBracket: {
                            context->next_token_index += 1;

                            auto last_range = token_range(*context, token);

                            expect(token, next_token(*context));

                            Expression* return_type;
                            if(token.type == TokenType::Arrow) {
                                context->next_token_index += 1;

                                expect(expression, parse_expression(context));

                                return_type = heapify(expression);
                                last_range = expression.range;
                            } else {
                                return_type = nullptr;
                            }

                            Expression expression;
                            expression.type = ExpressionType::FunctionType;
                            expression.range = span_range(first_range, last_range);
                            expression.function_type = {
                                {},
                                return_type
                            };

                            append(expression_stack, expression);
                        } break;

                        case TokenType::Identifier: {
                            context->next_token_index += 1;

                            auto identifier = identifier_from_token(*context, token);

                            expect(token, next_token(*context));

                            if(token.type == TokenType::Colon) {
                                List<FunctionParameter> parameters{};

                                expect(parameter, parse_function_parameter_second_half(context, identifier));

                                append(&parameters, parameter);

                                expect(token, next_token(*context));

                                FileRange last_range;
                                switch(token.type) {
                                    case TokenType::Comma: {
                                        context->next_token_index += 1;

                                        while(true) {
                                            expect(identifier, expect_identifier(context));

                                            expect(parameter, parse_function_parameter_second_half(context, identifier));

                                            append(&parameters, parameter);

                                            expect(token, next_token(*context));

                                            auto done = false;
                                            switch(token.type) {
                                                case TokenType::Comma: {
                                                    context->next_token_index += 1;
                                                } break;

                                                case TokenType::CloseRoundBracket: {
                                                    context->next_token_index += 1;

                                                    done = true;
                                                } break;

                                                default: {
                                                    error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                                    return { false };
                                                } break;
                                            }

                                            if(done) {
                                                last_range = token_range(*context, token);

                                                break;
                                            }
                                        }
                                    } break;

                                    case TokenType::CloseRoundBracket: {
                                        context->next_token_index += 1;

                                        last_range = token_range(*context, token);
                                    } break;

                                    default: {
                                        error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                        return { false };
                                    } break;
                                }

                                expect(token, next_token(*context));

                                Expression *return_type;
                                if(token.type == TokenType::Arrow) {
                                    context->next_token_index;

                                    expect(expression, parse_expression(context));

                                    return_type = heapify(expression);
                                    last_range = expression.range;
                                } else {
                                    return_type = nullptr;
                                }

                                Expression expression;
                                expression.type = ExpressionType::FunctionType;
                                expression.range = span_range(first_range, last_range);
                                expression.function_type = {
                                    to_array(parameters),
                                    return_type
                                };

                                append(expression_stack, expression);
                            } else {
                                auto expression = named_reference_from_identifier(identifier);

                                List<Operation> sub_operation_stack{};
                                List<Expression> sub_expression_stack{};

                                append(&sub_expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, true));

                                if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                    return { false };
                                }

                                append(expression_stack, right_expression);
                            }
                        } break;

                        default: {
                            expect(expression, parse_expression(context));

                            if(!expect_basic_token(context, TokenType::CloseRoundBracket)) {
                                return { false };
                            }

                            append(expression_stack, expression);
                        } break;
                    }
                } break;

                case TokenType::OpenCurlyBracket: {
                    context->next_token_index += 1;

                    auto first_range = token_range(*context, token);

                    expect(token, next_token(*context));

                    switch(token.type) {
                        case TokenType::CloseCurlyBracket: {
                            context->next_token_index += 1;

                            Expression expression;
                            expression.type = ExpressionType::ArrayLiteral;
                            expression.range = span_range(first_range, token_range(*context, token));
                            expression.array_literal = {};

                            append(expression_stack, expression);
                        } break;

                        case TokenType::Identifier: {
                            context->next_token_index += 1;

                            auto identifier = identifier_from_token(*context, token);

                            expect(token, next_token(*context));

                            switch(token.type) {
                                case TokenType::Equals: {
                                    context->next_token_index += 1;

                                    expect(expression, parse_expression(context));

                                    List<StructLiteralMember> members{};

                                    append(&members, {
                                        identifier,
                                        expression
                                    });

                                    expect(token, next_token(*context));

                                    FileRange last_range;
                                    switch(token.type) {
                                        case TokenType::Comma: {
                                            context->next_token_index += 1;

                                            while(true) {
                                                expect(identifier, expect_identifier(context));

                                                if(!expect_basic_token(context, TokenType::Equals)) {
                                                    return { false };
                                                }

                                                expect(expression, parse_expression(context));

                                                append(&members, {
                                                    identifier,
                                                    expression
                                                });

                                                expect(token, next_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        context->next_token_index += 1;
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        context->next_token_index += 1;

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                                        return { false };
                                                    } break;
                                                }

                                                if(done) {
                                                    auto last_range = token_range(*context, token);

                                                    break;
                                                }
                                            }
                                        } break;

                                        case TokenType::CloseCurlyBracket: {
                                            context->next_token_index += 1;

                                            last_range = token_range(*context, token);
                                        } break;

                                        default: {
                                            error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                            return { false };
                                        } break;
                                    }

                                    Expression expression;
                                    expression.type = ExpressionType::StructLiteral;
                                    expression.range = span_range(first_range, last_range);
                                    expression.struct_literal = to_array(members);

                                    append(expression_stack, expression);
                                } break;

                                case TokenType::CloseCurlyBracket: {
                                    context->next_token_index += 1;

                                    auto first_element = named_reference_from_identifier(identifier);

                                    Expression expression;
                                    expression.type = ExpressionType::ArrayLiteral;
                                    expression.range = span_range(first_range, token_range(*context, token));
                                    expression.array_literal = {
                                        1,
                                        heapify(first_element)
                                    };

                                    append(expression_stack, expression);
                                } break;

                                default: {
                                    auto sub_expression = named_reference_from_identifier(identifier);

                                    List<Operation> sub_operation_stack{};
                                    List<Expression> sub_expression_stack{};

                                    append(&sub_expression_stack, sub_expression);

                                    expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, true));

                                    List<Expression> elements{};

                                    append(&elements, right_expression);

                                    expect(token, next_token(*context));

                                    FileRange last_range;
                                    switch(token.type) {
                                        case TokenType::Comma: {
                                            context->next_token_index += 1;

                                            while(true) {
                                                expect(expression, parse_expression(context));

                                                append(&elements, expression);

                                                expect(token, next_token(*context));

                                                auto done = false;
                                                switch(token.type) {
                                                    case TokenType::Comma: {
                                                        context->next_token_index += 1;
                                                    } break;

                                                    case TokenType::CloseCurlyBracket: {
                                                        context->next_token_index += 1;

                                                        done = true;
                                                    } break;

                                                    default: {
                                                        error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                                        return { false };
                                                    } break;
                                                }

                                                if(done) {
                                                    auto last_range = token_range(*context, token);

                                                    break;
                                                }
                                            }
                                        } break;

                                        case TokenType::CloseCurlyBracket: {
                                            context->next_token_index += 1;

                                            last_range = token_range(*context, token);
                                        } break;
                                    }

                                    Expression expression;
                                    expression.type = ExpressionType::ArrayLiteral;
                                    expression.range = span_range(first_range, last_range);
                                    expression.array_literal = to_array(elements);

                                    append(expression_stack, expression);
                                } break;
                            }
                        } break;

                        default: {
                            expect(expression, parse_expression(context));

                            List<Expression> elements{};

                            append(&elements, expression);

                            expect(token, next_token(*context));

                            FileRange last_range;
                            switch(token.type) {
                                case TokenType::Comma: {
                                    context->next_token_index += 1;

                                    while(true) {
                                        expect(expression, parse_expression(context));

                                        append(&elements, expression);

                                        expect(token, next_token(*context));

                                        auto done = false;
                                        switch(token.type) {
                                            case TokenType::Comma: {
                                                context->next_token_index += 1;
                                            } break;

                                            case TokenType::CloseCurlyBracket: {
                                                context->next_token_index += 1;

                                                done = true;
                                            } break;

                                            default: {
                                                error(*context, "Expected ',' or '}'. Got '%s'", get_token_text(token));

                                                return { false };
                                            } break;
                                        }

                                        if(done) {
                                            auto last_range = token_range(*context, token);

                                            break;
                                        }
                                    }
                                } break;

                                case TokenType::CloseCurlyBracket: {
                                    context->next_token_index += 1;

                                    last_range = token_range(*context, token);
                                } break;
                            }

                            Expression expression;
                            expression.type = ExpressionType::ArrayLiteral;
                            expression.range = span_range(first_range, last_range);
                            expression.array_literal = to_array(elements);

                            append(expression_stack, expression);
                        } break;
                    }
                } break;

                default: {
                    error(*context, "Expected an expression. Got '%s'", get_token_text(token));

                    return { false };
                } break;
            }
        }

        if(context->next_token_index == context->tokens.count) {
            break;
        }

        auto token = context->tokens[context->next_token_index];

        auto first_range = token_range(*context, token);

        Operation operation;

        // Parse left-recursive expressions (e.g. binary operators) after parsing all adjacent non-left-recursive expressions
        auto done = false;
        switch(token.type) {
            case TokenType::Dot: {
                context->next_token_index += 1;

                expect(identifier, expect_identifier(context));

                operation.type = OperationType::MemberReference;
                operation.range = first_range;
                operation.member_reference = identifier;

                expect_left_recursive = true;
            } break;

            case TokenType::Plus: {
                context->next_token_index += 1;

                operation.type = OperationType::Addition;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Dash: {
                context->next_token_index += 1;

                operation.type = OperationType::Subtraction;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Asterisk: {
                context->next_token_index += 1;

                operation.type = OperationType::Multiplication;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::ForwardSlash: {
                context->next_token_index += 1;

                operation.type = OperationType::Division;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Percent: {
                context->next_token_index += 1;

                operation.type = OperationType::Modulo;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Ampersand: {
                context->next_token_index += 1;

                operation.type = OperationType::BitwiseAnd;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoubleAmpersand: {
                context->next_token_index += 1;

                operation.type = OperationType::BooleanAnd;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::Pipe: {
                context->next_token_index += 1;

                operation.type = OperationType::BitwiseOr;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoublePipe: {
                context->next_token_index += 1;

                operation.type = OperationType::BooleanOr;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::DoubleEquals: {
                context->next_token_index += 1;

                operation.type = OperationType::Equal;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::BangEquals: {
                context->next_token_index += 1;

                operation.type = OperationType::NotEqual;
                operation.range = first_range;

                expect_left_recursive = false;
            } break;

            case TokenType::OpenRoundBracket: {
                context->next_token_index += 1;

                List<Expression> parameters{};

                expect(token, next_token(*context));

                FileRange last_range;
                if(token.type == TokenType::CloseRoundBracket) {
                    context->next_token_index += 1;

                    last_range = token_range(*context, token);
                } else {
                    while(true) {
                        expect(expression, parse_expression(context));

                        append(&parameters, expression);

                        expect(token, next_token(*context));

                        auto done = false;
                        switch(token.type) {
                            case TokenType::Comma: {
                                context->next_token_index += 1;
                            } break;

                            case TokenType::CloseRoundBracket: {
                                context->next_token_index += 1;

                                done = true;
                            } break;

                            default: {
                                error(*context, "Expected ',' or ')'. Got '%s'", get_token_text(token));

                                return { false };
                            } break;
                        }

                        if(done) {
                            last_range = token_range(*context, token);

                            break;
                        }
                    }
                }

                operation.type = OperationType::FunctionCall;
                operation.range = span_range(first_range, last_range);
                operation.function_call = to_array(parameters);

                expect_left_recursive = true;
            } break;

            case TokenType::OpenSquareBracket: {
                context->next_token_index += 1;

                expect(expression, parse_expression(context));

                expect(token, next_token(*context));

                if(token.type != TokenType::CloseSquareBracket) {
                    error(*context, "Expected ']'. Got '%s'", get_token_text(token));

                    return { false };
                }

                operation.type = OperationType::IndexReference;
                operation.range = span_range(first_range, token_range(*context, token));
                operation.index_reference = expression;

                expect_left_recursive = true;
            } break;

            default: {
                done = true;
            } break;
        }

        if(done) {
            break;
        }

        // Meat of the precedence sorting
        while(true) {
            if(operation_stack->count == 0) {
                append(operation_stack, operation);

                break;
            } else {
                auto last_operation = (*operation_stack)[operation_stack->count - 1];

                if(operation_precedences[(int)operation.type] < operation_precedences[(int)last_operation.type]) {
                    append(operation_stack, operation);

                    break;
                } else {
                    apply_operation(expression_stack, take_last(operation_stack));
                }
            }
        }
    }

    // Apply the remaining operations
    while(operation_stack->count != 0) {
        apply_operation(expression_stack, take_last(operation_stack));
    }

    return {
        true,
        (*expression_stack)[0]
    };
}

static Result<Expression> parse_expression(Context *context) {
    List<Operation> operation_stack{};

    List<Expression> expression_stack{};

    return parse_right_expressions(context, &operation_stack, &expression_stack, true);
}

static Result<Statement> parse_expression_statement_or_variable_assignment(Context *context, Expression expression) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    switch(character) {
        case '=': {
            context->character += 1;

            auto last_line = context->line;
            auto last_character = context->character;

            auto character = fgetc(context->source_file);

            switch(character) {
                case '=': {
                    context->character += 1;

                    skip_whitespace(context);

                    List<Operation> operation_stack{};

                    Operation operation;
                    operation.type = OperationType::Equal;
                    operation.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };

                    append(&operation_stack, operation);

                    List<Expression> expression_stack{};

                    append(&expression_stack, expression);

                    expect(right_expressions, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                    skip_whitespace(context);

                    last_line = context->line;
                    last_character = context->character;

                    if(!expect_character(context, ';')) {
                        return { false };
                    }

                    Statement statement;
                    statement.type = StatementType::Expression;
                    statement.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };

                    statement.expression.type = ExpressionType::BinaryOperation;
                    statement.expression.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        right_expressions.range.end_line,
                        right_expressions.range.end_character
                    };
                    statement.expression.binary_operation.binary_operator = BinaryOperator::Equal;
                    statement.expression.binary_operation.left = heapify(expression);
                    statement.expression.binary_operation.right = heapify(right_expressions);

                    return {
                        true,
                        statement
                    };
                } break;

                case EOF: {
                    error(*context, "Unexpected End of File");

                    return { false };
                } break;

                default: {
                    ungetc(character, context->source_file);

                    skip_whitespace(context);

                    expect(value_expression, parse_expression(context));

                    skip_whitespace(context);

                    auto last_line = context->line;
                    auto last_character = context->character;

                    if(!expect_character(context, ';')) {
                        return { false };
                    }

                    Statement statement;
                    statement.type = StatementType::Assignment;
                    statement.range = {
                        context->source_file_path,
                        first_line,
                        first_character,
                        last_line,
                        last_character
                    };
                    statement.assignment.target = expression;
                    statement.assignment.value = value_expression;

                    return {
                        true,
                        statement
                    };
                } break;
            }
        } break;
        
        case ';': {
            context->character += 1;

            Statement statement;
            statement.type = StatementType::Expression;
            statement.range = {
                context->source_file_path,
                first_line,
                first_character,
                first_line,
                first_character
            };
            statement.expression = expression;

            return {
                true,
                statement
            };
        } break;
        
        case EOF: {
            error(*context, "Unexpected End of File");

            return { false };
        } break;

        default: {
            error(*context, "Expected '=' or ';'. Got '%c'", character);

            return { false };
        } break;
    }
}

static Result<Statement> parse_statement(Context *context);

static Result<Statement> continue_parsing_function_declaration(Context *context, Identifier name, Array<FunctionParameter> parameters, FileRange parameters_range) {
    auto character = fgetc(context->source_file);

    auto is_external = false;
    auto has_return_type = false;
    Expression return_type;
    if(character == '-') {
        context->character += 1;

        if(!expect_character(context, '>')) {
            return { false };
        }

        skip_whitespace(context);

        expect(expression, parse_expression(context));

        has_return_type = true;
        return_type = expression;

        skip_whitespace(context);
        
        character = fgetc(context->source_file);

        if(isalpha(character) || character == '_') {
            ungetc(character, context->source_file);

            auto identifier = parse_identifier(context);

            if(strcmp(identifier.text, "extern") != 0) {
                error(*context, "Expected 'extern', ';' or '{', got '%s'", identifier.text);

                return { false };
            }

            character = fgetc(context->source_file);

            is_external = true;
        }
    } else if(isalpha(character) || character == '_') {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        if(strcmp(identifier.text, "extern") != 0) {
            error(*context, "Expected 'extern', '-', ';' or '{', got '%s'", identifier.text);

            return { false };
        }

        skip_whitespace(context);

        character = fgetc(context->source_file);

        is_external = true;
    }

    if(character == '{') {
        context->character += 1;

        skip_whitespace(context);
        
        Array<Statement> statements;
        if(!is_external) {
            List<Statement> statement_list{};

            while(true) {
                auto character = fgetc(context->source_file);

                if(character == '}') {
                    break;
                } else {
                    ungetc(character, context->source_file);
                }

                expect(statement, parse_statement(context));

                append(&statement_list, statement);

                skip_whitespace(context);
            }

            statements = to_array(statement_list);
        }

        Statement statement;
        statement.type = StatementType::FunctionDeclaration;
        statement.range = name.range;
        statement.function_declaration.name = name;
        statement.function_declaration.parameters = parameters;
        statement.function_declaration.has_return_type = has_return_type;
        statement.function_declaration.is_external = is_external;

        if(has_return_type) {
            statement.function_declaration.return_type = return_type;
        }

        if(!is_external) {
            statement.function_declaration.statements = statements;
        }

        return {
            true,
            statement
        };
    } else if(character == ';') {
        context->character += 1;

        if(is_external) {
            Statement statement;
            statement.type = StatementType::FunctionDeclaration;
            statement.range = name.range;
            statement.function_declaration.name = name;
            statement.function_declaration.parameters = parameters;
            statement.function_declaration.has_return_type = has_return_type;
            statement.function_declaration.is_external = true;

            if(has_return_type) {
                statement.function_declaration.return_type = return_type;
            }

            return {
                true,
                statement
            };
        } else {
            Expression expression;
            expression.type = ExpressionType::FunctionType;
            expression.range = parameters_range;
            expression.function_type.parameters = parameters;

            if(has_return_type) {
                expression.function_type.return_type = heapify(return_type);
            } else {
                expression.function_type.return_type = nullptr;
            }

            Statement statement;
            statement.type = StatementType::ConstantDefinition;
            statement.range = name.range;
            statement.constant_definition = {
                name,
                expression
            };

            return {
                true,
                statement
            };
        }
    } else if(character == EOF) {
        error(*context, "Unexpected End of File");

        return { false };
    } else {
        error(*context, "Expected 'extern', '-', ';' or '{', got '%c'", character);

        return { false };
    }
}

static Result<Statement> parse_statement(Context *context) {
    auto first_line = context->line;
    auto first_character = context->character;

    auto character = fgetc(context->source_file);

    if(isalpha(character) || character == '_') {
        ungetc(character, context->source_file);

        auto identifier = parse_identifier(context);

        skip_whitespace(context);

        if(strcmp(identifier.text, "if") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);

            if(!expect_character(context, '{')) {
                return { false };
            }

            List<Statement> statements{};

            while(true) {
                skip_whitespace(context);

                auto character = fgetc(context->source_file);

                if(character == '}') {
                    break;
                } else {
                    ungetc(character, context->source_file);
                }

                expect(statment, parse_statement(context));

                append(&statements, statment);
            }

            Statement statement;
            statement.type = StatementType::LoneIf;
            statement.range = expression.range;
            statement.lone_if.condition = expression;
            statement.lone_if.statements = to_array(statements);

            return {
                true,
                statement
            };
        } else if(strcmp(identifier.text, "while") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);

            if(!expect_character(context, '{')) {
                return { false };
            }

            List<Statement> statements{};

            while(true) {
                skip_whitespace(context);

                auto character = fgetc(context->source_file);

                if(character == '}') {
                    break;
                } else {
                    ungetc(character, context->source_file);
                }

                expect(statement, parse_statement(context));

                append(&statements, statement);
            }

            Statement statement;
            statement.type = StatementType::WhileLoop;
            statement.range = expression.range;
            statement.while_loop.condition = expression;
            statement.while_loop.statements = to_array(statements);

            return {
                true,
                statement
            };
        } else if(strcmp(identifier.text, "return") == 0) {
            expect(expression, parse_expression(context));

            skip_whitespace(context);
            
            auto last_line = context->line;
            auto last_character = context->character;

            if(!expect_character(context, ';')) {
                return { false };
            }

            Statement statement;
            statement.type = StatementType::Return;
            statement.range = {
                context->source_file_path,
                first_line,
                first_character,
                last_line,
                last_character
            };
            statement._return = expression;

            return {
                true,
                statement
            };
        } else {
            auto after_identifier_line = context->line;
            auto after_identifier_character = context->character;

            auto character = fgetc(context->source_file);

            switch(character) {
                case ':': {
                    context->character += 1;

                    auto character = fgetc(context->source_file);

                    if(character == ':') {
                        context->character += 1;

                        skip_whitespace(context);

                        auto character = fgetc(context->source_file);
            
                        auto value_line = context->line;
                        auto value_character = context->character;

                        if(isalpha(character) || character == '_'){
                            ungetc(character, context->source_file);

                            auto value_identifier = parse_identifier(context);

                            if(strcmp(value_identifier.text, "struct") == 0) {
                                skip_whitespace(context);

                                if(!expect_character(context, '{')) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                List<StructMember> members{};

                                auto character = fgetc(context->source_file);

                                if(character == '}') {
                                    context->character += 1;
                                } else {
                                    ungetc(character, context->source_file);

                                    while(true) {
                                        expect(name, expect_identifier(context));

                                        skip_whitespace(context);

                                        if(!expect_character(context, ':')) {
                                            return { false };
                                        }

                                        skip_whitespace(context);

                                        expect(type, parse_expression(context));

                                        append(&members, {
                                            name,
                                            type
                                        });

                                        skip_whitespace(context);

                                        auto character = fgetc(context->source_file);

                                        if(character == '}') {
                                            context->character += 1;

                                            break;
                                        } else if(character == ',') {
                                            context->character += 1;

                                            skip_whitespace(context);
                                        } else if(character == EOF) {

                                        } else if(character == EOF) {
                                            error(*context, "Unexpected End of File");

                                            return { false };
                                        } else {
                                            error(*context, "Expected ',' or ';'. Got '%c'", character);

                                            return { false };
                                        }
                                    }
                                }

                                Statement statement;
                                statement.type = StatementType::StructDefinition;
                                statement.range = identifier.range;
                                statement.struct_definition = {
                                    identifier,
                                    to_array(members)
                                };

                                return {
                                    true,
                                    statement
                                };
                            } else {
                                Expression expression;
                                expression.type = ExpressionType::NamedReference;
                                expression.range = value_identifier.range;
                                expression.named_reference = value_identifier;

                                List<Operation> operation_stack{};
                                
                                List<Expression> expression_stack{};

                                append(&expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                                skip_whitespace(context);

                                if(!expect_character(context, ';')) {
                                    return { false };
                                }

                                Statement statement;
                                statement.type = StatementType::ConstantDefinition;
                                statement.range = identifier.range;
                                statement.constant_definition = {
                                    identifier,
                                    right_expression
                                };

                                return {
                                    true,
                                    statement
                                };
                            }
                        } else if(character == '(') {
                            context->character += 1;

                            skip_whitespace(context);

                            auto last_line = context->line;
                            auto last_character = context->character;

                            auto character = fgetc(context->source_file);

                            if(isalpha(character) || character == '_') {
                                ungetc(character, context->source_file);

                                auto first_identifier = parse_identifier(context);

                                skip_whitespace(context);

                                auto character = fgetc(context->source_file);

                                if(character == ':') {
                                    context->character += 1;

                                    skip_whitespace(context);

                                    auto character = fgetc(context->source_file);

                                    FunctionParameter first_parameter = {
                                        first_identifier
                                    };

                                    switch(character) {
                                        context->character += 1;

                                        case '$': {
                                            skip_whitespace(context);

                                            expect(name, expect_identifier(context));

                                            first_parameter.is_polymorphic_determiner = true;
                                            first_parameter.polymorphic_determiner = name;
                                        } break;

                                        case EOF: {
                                            error(*context, "Unexpected End of File");

                                            return { false };
                                        } break;

                                        default: {
                                            ungetc(character, context->source_file);

                                            expect(expression, parse_expression(context));

                                            first_parameter.is_polymorphic_determiner = false;
                                            first_parameter.type = expression;
                                        } break;
                                    }

                                    skip_whitespace(context);

                                    List<FunctionParameter> parameters{};

                                    append(&parameters, first_parameter);
                                    
                                    unsigned int last_line;
                                    unsigned int last_character;

                                    while(true) {
                                        last_line = context->line;
                                        last_character = context->character;

                                        auto character = fgetc(context->source_file);

                                        if(character == ',') {
                                            context->character += 1;

                                            skip_whitespace(context);
                                        } else if(character == ')') {
                                            context->character += 1;

                                            break;
                                        } else if(character == EOF) {
                                            error(*context, "Unexpected End of File");

                                            return { false };
                                        } else {
                                            error(*context, "Expected ',' or ')', got '%c'", character);

                                            return { false };
                                        }

                                        expect(parameter, parse_function_parameter(context));

                                        append(&parameters, parameter);
                                    }

                                    skip_whitespace(context);

                                    FileRange parameters_range {
                                        context->source_file_path,
                                        value_line,
                                        value_character,
                                        last_character,
                                        last_line
                                    };

                                    return continue_parsing_function_declaration(context, identifier, to_array(parameters), parameters_range);
                                } else {
                                    ungetc(character, context->source_file);

                                    skip_whitespace(context);

                                    Expression expression;
                                    expression.type = ExpressionType::NamedReference;
                                    expression.range = first_identifier.range;
                                    expression.named_reference = first_identifier;

                                    List<Operation> sub_operation_stack{};

                                    List<Expression> sub_expression_stack{};

                                    append(&sub_expression_stack, expression);

                                    expect(right_expression, parse_right_expressions(context, &sub_operation_stack, &sub_expression_stack, false));

                                    skip_whitespace(context);

                                    if(!expect_character(context, ')')) {
                                        return { false };
                                    }

                                    List<Operation> operation_stack{};

                                    List<Expression> expression_stack{};

                                    append(&expression_stack, right_expression);

                                    expect(outer_right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));
                                    
                                    auto last_line = context->line;
                                    auto last_character = context->character;

                                    if(!expect_character(context, ';')) {
                                        return { false };
                                    }

                                    Statement statement;
                                    statement.type = StatementType::ConstantDefinition;
                                    statement.range = {
                                        context->source_file_path,
                                        first_line,
                                        first_character,
                                        last_line,
                                        last_character
                                    };
                                    statement.constant_definition = {
                                        identifier,
                                        outer_right_expression
                                    };

                                    return {
                                        true,
                                        statement
                                    };
                                }
                            } else if(character == ')') {
                                context->character += 1;

                                skip_whitespace(context);

                                FileRange parameters_range {
                                    context->source_file_path,
                                    value_line,
                                    value_character,
                                    last_line,
                                    last_character
                                };

                                return continue_parsing_function_declaration(context, identifier, Array<FunctionParameter>{}, parameters_range);
                            } else {
                                ungetc(character, context->source_file);

                                skip_whitespace(context);

                                expect(expression, parse_expression(context));

                                if(!expect_character(context, ')')) {
                                    return { false };
                                }

                                skip_whitespace(context);

                                List<Operation> operation_stack{};

                                List<Expression> expression_stack{};

                                append(&expression_stack, expression);

                                expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                                auto last_line = context->line;
                                auto last_character = context->character;

                                if(!expect_character(context, ';')) {
                                    return { false };
                                }

                                Statement statement;
                                statement.type = StatementType::ConstantDefinition;
                                statement.range = {
                                    context->source_file_path,
                                    first_line,
                                    first_character,
                                    last_line,
                                    last_character
                                };
                                statement.constant_definition = {
                                    identifier,
                                    right_expression
                                };

                                return {
                                    true,
                                    statement
                                };
                            }
                        } else {
                            ungetc(character, context->source_file);

                            expect(expression, parse_expression(context));
                            
                            auto last_line = context->line;
                            auto last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::ConstantDefinition;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };
                            statement.constant_definition = {
                                identifier,
                                expression
                            };

                            return {
                                true,
                                statement
                            };
                        }
                    } else {
                        ungetc(character, context->source_file);

                        skip_whitespace(context);

                        auto character = fgetc(context->source_file);

                        bool has_type;
                        Expression type;
                        if(character != '=') {
                            ungetc(character, context->source_file);

                            expect(expression, parse_expression(context));

                            has_type = true;
                            type = expression;

                            skip_whitespace(context);

                            character = fgetc(context->source_file);
                        } else {
                            has_type = false;
                        }

                        auto last_line = context->line;
                        auto last_character = context->character;

                        bool has_initializer;
                        Expression initializer;
                        if(character == '=') {
                            context->character += 1;

                            skip_whitespace(context);

                            expect(expression, parse_expression(context));

                            has_initializer = true;
                            initializer = expression;

                            skip_whitespace(context);
                            
                            last_line = context->line;
                            last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }
                        } else if(character == ';') {
                            context->character += 1;

                            has_initializer = false;
                        } else if(character == EOF) {
                            error(*context, "Unexpected End of File");

                            return { false };
                        } else {
                            error(*context, "Expected '=' or ';', got '%c'", character);

                            return { false };
                        }

                        Statement statement;
                        statement.type = StatementType::VariableDeclaration;
                        statement.range = {
                            context->source_file_path,
                            first_line,
                            first_character,
                            last_line,
                            last_character
                        };
                        statement.variable_declaration.name = identifier;

                        if(has_type && has_initializer) {
                            statement.variable_declaration.type = VariableDeclarationType::FullySpecified;

                            statement.variable_declaration.fully_specified = {
                                type,
                                initializer
                            };
                        } else if(has_type) {
                            statement.variable_declaration.type = VariableDeclarationType::Uninitialized;

                            statement.variable_declaration.uninitialized = type;
                        } else if(has_initializer) {
                            statement.variable_declaration.type = VariableDeclarationType::TypeElided;

                            statement.variable_declaration.type_elided = initializer;
                        } else {
                            abort();
                        }

                        return {
                            true,
                            statement
                        };
                    }
                } break;

                case '=': {
                    context->character += 1;

                    Expression expression;
                    expression.type = ExpressionType::NamedReference;
                    expression.range = identifier.range;
                    expression.named_reference = identifier;
                    
                    auto last_line = context->line;
                    auto last_character = context->character;

                    auto character = fgetc(context->source_file);

                    switch(character) {
                        case '=': {
                            context->character += 1;

                            skip_whitespace(context);

                            List<Operation> operation_stack{};

                            Operation operation;
                            operation.type = OperationType::Equal;
                            operation.range = {
                                context->source_file_path,
                                after_identifier_line,
                                after_identifier_character,
                                last_line,
                                last_character
                            };

                            append(&operation_stack, operation);

                            List<Expression> expression_stack{};

                            append(&expression_stack, expression);

                            expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, true));

                            skip_whitespace(context);
                            
                            last_line = context->line;
                            last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::Expression;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };

                            statement.expression.type = ExpressionType::BinaryOperation;
                            statement.expression.range = {
                                context->source_file_path,
                                after_identifier_line,
                                after_identifier_character,
                                right_expression.range.end_line,
                                right_expression.range.end_character
                            };
                            statement.expression.binary_operation.binary_operator = BinaryOperator::Equal;
                            statement.expression.binary_operation.left = heapify(expression);
                            statement.expression.binary_operation.right = heapify(right_expression);

                            return {
                                true,
                                statement
                            };
                        } break;

                        case EOF: {
                            error(*context, "Unexpected End of File");

                            return { false };
                        } break;

                        default: {
                            ungetc(character, context->source_file);

                            skip_whitespace(context);

                            expect(value_expression, parse_expression(context));

                            skip_whitespace(context);
                            
                            auto last_line = context->line;
                            auto last_character = context->character;

                            if(!expect_character(context, ';')) {
                                return { false };
                            }

                            Statement statement;
                            statement.type = StatementType::Assignment;
                            statement.range = {
                                context->source_file_path,
                                first_line,
                                first_character,
                                last_line,
                                last_character
                            };
                            statement.assignment.target = expression;
                            statement.assignment.value = value_expression;

                            return {
                                true,
                                statement
                            };
                        } break;
                    }
                } break;

                case EOF: {
                    error(*context, "Unexpected End of File");

                    return { false };
                } break;

                default: {
                    ungetc(character, context->source_file);

                    Expression expression;
                    expression.range = identifier.range;
                    expression.type = ExpressionType::NamedReference;
                    expression.named_reference = identifier;

                    List<Operation> operation_stack{};

                    List<Expression> expression_stack{};

                    append(&expression_stack, expression);

                    expect(right_expression, parse_right_expressions(context, &operation_stack, &expression_stack, false));

                    skip_whitespace(context);

                    expect(statement, parse_expression_statement_or_variable_assignment(context, right_expression));

                    return {
                        true,
                        statement
                    };
                } break;
            }
        }
    } else if(character == '#') {
        context->character += 1;

        auto character = fgetc(context->source_file);

        if(isalpha(character) || character == '_') {
            ungetc(character, context->source_file);

            auto identifier = parse_identifier(context);

            if(strcmp(identifier.text, "import") == 0) {
                skip_whitespace(context);

                expect(string, parse_string(context));

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                auto import_path = allocate<char>(string.text.count + 1);
                memcpy(import_path, string.text.elements, string.text.count);
                import_path[string.text.count] = 0;

                auto source_file_directory = path_get_directory_component(context->source_file_path);

                auto import_path_relative = allocate<char>(strlen(source_file_directory) + string.text.count + 1);

                strcpy(import_path_relative, source_file_directory);
                strcat(import_path_relative, import_path);

                expect(absolute, path_relative_to_absolute(import_path_relative));

                append<const char *>(context->remaining_files, absolute);

                Statement statement;
                statement.type = StatementType::Import;
                statement.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    string.range.end_line,
                    string.range.end_character
                };
                statement.import = absolute;

                return {
                    true,
                    statement
                };
            } else if(strcmp(identifier.text, "library") == 0) {
                skip_whitespace(context);

                expect(string, parse_string(context));

                skip_whitespace(context);

                if(!expect_character(context, ';')) {
                    return { false };
                }

                auto library = allocate<char>(string.text.count + 1);
                memcpy(library, string.text.elements, string.text.count);
                library[string.text.count] = 0;

                Statement statement;
                statement.type = StatementType::Library;
                statement.range = {
                    context->source_file_path,
                    first_line,
                    first_character,
                    string.range.end_line,
                    string.range.end_character
                };
                statement.library = library;

                return {
                    true,
                    statement
                };
            } else {
                error(*context, "Expected 'import' or 'library'. Got '%s'", identifier.text);

                return { false };
            }
        } else if(character == EOF) {
            error(*context, "Unexpected End of File");

            return { false };
        } else {
            error(*context, "Expected a-z or A-Z. Got '%c'", (char)character);

            return { false };
        }
    } else {
        ungetc(character, context->source_file);

        expect(expression, parse_expression(context));

        expect(statement, parse_expression_statement_or_variable_assignment(context, expression));

        return {
            true,
            statement
        };
    }
}

void set_statement_parents(Statement *statement) {
    switch(statement->type) {
        case StatementType::FunctionDeclaration: {
            if(!statement->function_declaration.is_external) {
                for(auto &child : statement->function_declaration.statements) {
                    child.is_top_level = false;
                    child.parent = statement;

                    set_statement_parents(&child);
                }
            }
        } break;

        case StatementType::LoneIf: {
            for(auto &child : statement->lone_if.statements) {
                child.is_top_level = false;
                child.parent = statement;

                set_statement_parents(&child);
            }
        } break;
       
        case StatementType::WhileLoop: {
            for(auto &child : statement->while_loop.statements) {
                child.is_top_level = false;
                child.parent = statement;

                set_statement_parents(&child);
            }
        } break;
    }
}

Result<Array<Statement>> parse_tokens(const char *path, Array<Token> tokens) {

}